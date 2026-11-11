/*
 * 中文：电量日志的可重放统计与有界文件输出；累计值是采样估算，空白和锁屏不外推。
 * English: Replayable battery logging and bounded file output; totals are sampled estimates without extrapolating gaps or locked waits.
 */
#include "battery_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool critical(uint32_t events) {
    return (events & (BATT_LOG_BEGIN | BATT_LOG_END | BATT_LOG_FULL | BATT_LOG_LOW |
                     BATT_LOG_OFF | BATT_LOG_RESTART | BATT_LOG_UNDERVOLTAGE)) != 0;
}

bool batt_log_retain(batt_log_state_t *s, const batt_log_record_t *record) {
    const bool lost = s->count == BATT_LOG_QUEUE_SIZE;
    if (lost) {
        unsigned remove_at = 0;
        while (remove_at < s->count &&
               critical(s->queue[(s->head + remove_at) % BATT_LOG_QUEUE_SIZE].events)) ++remove_at;
        ++s->dropped;
        if (remove_at == s->count) {
            if (!critical(record->events)) return true;
            remove_at = 0;
        }
        for (unsigned i = remove_at; i + 1 < s->count; ++i)
            s->queue[(s->head + i) % BATT_LOG_QUEUE_SIZE] =
                s->queue[(s->head + i + 1) % BATT_LOG_QUEUE_SIZE];
        --s->count;
    }
    s->queue[(s->head + s->count++) % BATT_LOG_QUEUE_SIZE] = *record;
    return lost;
}

static void enqueue(batt_log_state_t *s, uint32_t events, int64_t now, int64_t wall) {
    batt_log_record_t record = {
        .sample = s->latest, .have_sample = s->have_sample, .locked = s->locked,
        .session = s->session, .seq = ++s->seq, .events = events, .dropped = s->dropped,
        .up_us = now, .wall_sec = wall, .covered_us = s->covered_us,
        .unknown_us = s->unknown_us, .locked_us = s->locked_us,
        .in_ma_us = s->in_ma_us, .out_ma_us = s->out_ma_us,
        .stats = s->stats, .low_mask = s->low_mask,
    };
    if (s->count == BATT_LOG_QUEUE_SIZE) ++record.dropped;
    (void)batt_log_retain(s, &record);
}

// 分别积分正负电流；跨零时按线性过零点分割，不能用净值抵消。
// Integrate each current direction separately, splitting linear zero crossings rather than cancelling net charge.
static void integrate(batt_log_state_t *s, int64_t dt, int32_t a, int32_t b) {
    if (a >= 0 && b >= 0) s->in_ma_us += (a + b) * dt / 2;
    else if (a <= 0 && b <= 0) s->out_ma_us -= (a + b) * dt / 2;
    else {
        const int64_t aa = a < 0 ? -(int64_t)a : a;
        const int64_t bb = b < 0 ? -(int64_t)b : b;
        const int64_t first = aa * aa * dt / (2 * (aa + bb));
        const int64_t second = bb * bb * dt / (2 * (aa + bb));
        s->in_ma_us += a > 0 ? first : second;
        s->out_ma_us += a < 0 ? first : second;
    }
}

void batt_log_set_enabled(batt_log_state_t *s, bool enabled, int64_t now, int64_t wall) {
    if (s->enabled == enabled) return;
    if (!enabled) {
        batt_log_note(s, BATT_LOG_END, now, wall);
        s->enabled = false;
        return;
    }
    s->enabled = true;
    ++s->session;
    s->have_sample = s->previous_valid = false;
    s->have_cursor = true;
    s->cursor_us = now;
    s->covered_us = s->unknown_us = s->locked_us = 0;
    s->in_ma_us = s->out_ma_us = 0;
    s->last_period_us = now;
    s->last_cx_emit_us = now - BATT_LOG_PERIOD_US;
    s->low_mask = 0;
    s->stats = (batt_log_stats_t){.start_us = now};
    enqueue(s, BATT_LOG_BEGIN, now, wall);
}

static void observe_stats(batt_log_state_t *s, const batt_log_sample_t *v) {
    batt_log_stats_t *w = &s->stats;
    ++w->samples;
    if (s->locked) ++w->locked_samples;
    if (s->have_sample && v->up_us > s->latest.up_us &&
        v->up_us - s->latest.up_us > w->max_gap_us) w->max_gap_us = v->up_us - s->latest.up_us;
    if (v->have_v) {
        if (!w->v_count || v->raw_mv < w->v_min) w->v_min = v->raw_mv;
        if (!w->v_count || v->raw_mv > w->v_max) w->v_max = v->raw_mv;
        ++w->v_count;
        w->v_sum += v->raw_mv;
    }
    if (v->have_i) {
        if (!w->i_count || v->ma < w->i_min) w->i_min = v->ma;
        if (!w->i_count || v->ma > w->i_max) w->i_max = v->ma;
        ++w->i_count;
        w->i_sum += v->ma;
    }
    if (v->full_reset) {
        ++w->full_resets;
        w->full_reset_mask |= v->full_gate;
    }
}

void batt_log_observe(batt_log_state_t *s, const batt_log_sample_t *sample) {
    if (!s->enabled) return;
    uint32_t events = 0;
    const bool valid = sample->have_v && sample->have_i;
    // 保护事件可能插在读数与发布之间；旧采样可保留，但不能倒退积分游标。
    // Protection events may occur between a read and publication; retain the sample without rewinding accounting.
    const bool ordered = !s->have_cursor || sample->up_us >= s->cursor_us;
    const int64_t now = ordered ? sample->up_us : s->cursor_us;
    if (s->have_cursor && sample->up_us > s->cursor_us) {
        const int64_t dt = sample->up_us - s->cursor_us;
        if (!s->locked && valid && s->previous_valid && dt <= BATT_LOG_MAX_INTERVAL_US) {
            integrate(s, dt, s->latest.ma, sample->ma);
            s->covered_us += dt;
        } else {
            s->unknown_us += dt;
            if (s->locked) s->locked_us += dt;
            else if (dt > BATT_LOG_MAX_INTERVAL_US) events |= BATT_LOG_GAP;
        }
    }
    if (!s->have_sample) events |= BATT_LOG_SAMPLE;
    else {
        const batt_log_sample_t *p = &s->latest;
        if (p->have_v != sample->have_v || p->have_i != sample->have_i ||
            p->have_chg != sample->have_chg) events |= BATT_LOG_VALIDITY;
        const bool cx_changed = p->have_chg && sample->have_chg &&
                                p->charger_status != sample->charger_status;
        if (cx_changed) {
            if (!s->stats.cx_changes) s->stats.cx_first_us = now;
            ++s->stats.cx_changes;
            s->stats.cx_last_us = now;
        }
        // 输入、充电开关立即记；普通阶段抖动保留计数，每分钟至多额外写一次。
        // Record input/charging transitions immediately; summarize phase chatter at most once a minute.
        if (p->charging != sample->charging || (cx_changed &&
            (((p->charger_status ^ sample->charger_status) & ~0x18u) ||
             now - s->last_cx_emit_us >= BATT_LOG_PERIOD_US))) {
            events |= BATT_LOG_CHARGER;
            s->last_cx_emit_us = now;
        }
        if (p->charge_done != sample->charge_done || p->done_source != sample->done_source)
            events |= BATT_LOG_FULL;
    }
    observe_stats(s, sample);
    if (sample->charging) s->low_mask = 0;
    if (sample->have_v && sample->have_i && sample->have_chg &&
        !sample->charging && (sample->charger_status & 7) == 0 && sample->ma < 0) {
        uint8_t low = 0;
        const int thresholds[] = {3500, 3400, 3300, 3200};
        for (unsigned i = 0; i < sizeof(thresholds) / sizeof(thresholds[0]); ++i)
            if (sample->raw_mv <= thresholds[i]) low |= 1u << i;
        if (low & ~s->low_mask) events |= BATT_LOG_LOW;
        s->low_mask |= low;
    }
    s->latest = *sample;
    s->have_sample = s->have_cursor = true;
    s->previous_valid = valid && !s->locked && ordered;
    s->cursor_us = now;
    if (!s->locked && now - s->last_period_us >= BATT_LOG_PERIOD_US)
        events |= BATT_LOG_SAMPLE;
    if (events) enqueue(s, events, now, sample->wall_sec);
    if (events & BATT_LOG_SAMPLE) {
        s->last_period_us = now;
        s->stats = (batt_log_stats_t){.start_us = now};
    }
}

void batt_log_note(batt_log_state_t *s, uint32_t events, int64_t now, int64_t wall) {
    if (s->have_cursor && now < s->cursor_us) now = s->cursor_us;
    if (events & BATT_LOG_LOCK) s->previous_valid = false;
    if (s->enabled && s->have_cursor && now > s->cursor_us) {
        const int64_t dt = now - s->cursor_us;
        s->unknown_us += dt;
        if (s->locked) s->locked_us += dt;
    }
    if (events & BATT_LOG_LOCK) s->locked = true;
    if (events & BATT_LOG_WAKE) s->locked = false;
    if (!s->enabled) return;
    s->have_cursor = true;
    s->cursor_us = now;
    s->previous_valid = false;
    enqueue(s, events, now, wall);
}

bool batt_log_pop(batt_log_state_t *s, batt_log_record_t *record) {
    if (!s->count) return false;
    *record = s->queue[s->head];
    s->head = (s->head + 1) % BATT_LOG_QUEUE_SIZE;
    --s->count;
    return true;
}

int batt_log_format(char *line, size_t size, const batt_log_record_t *r, uint64_t boot,
                    int64_t write_us, uint32_t io_errors, uint32_t file_missed,
                    uint32_t file_seq) {
    const batt_log_sample_t *s = &r->sample;
    const bool v = r->have_sample && s->have_v;
    const bool i = r->have_sample && s->have_i;
    const bool chg = r->have_sample && s->have_chg;
    const int flags = (v ? 1 : 0) | (i ? 2 : 0) | (chg ? 4 : 0) |
                      (r->have_sample && s->ic_soc >= 0 ? 8 : 0);
    const int n = snprintf(line, size,
        "fmt=3 boot=%016llx session=%u seq=%u event=%u t=%lld up_ms=%lld "
        "sample_t=%lld sample_ms=%lld age_ms=%lld write_ms=%lld read_us=%u valid=%d "
        "raw=%d ma=%d chg=%d done=%d done_src=%u cx=%d comp=%ld ocv=%ld curve=%ld disp=%ld "
        "ic_soc=%d ic_age_ms=%lld locked=%d covered_ms=%lld unknown_ms=%lld lock_ms=%lld "
        "in_uah_est=%lld out_uah_est=%lld dropped=%u io_errors=%u file_missed=%u file_seq=%u "
        "low_mask=%u full_hold_ms=%ld full_gate=%u restore=%u "
        "win_ms=%lld n=%u locked_n=%u v_n=%u v_min=%ld v_avg=%lld v_max=%ld "
        "i_n=%u i_min=%ld i_avg=%lld i_max=%ld max_gap_ms=%lld "
        "cx_changes=%u cx_first_ms=%lld cx_last_ms=%lld full_resets=%u full_reset_mask=%u checkpoint_errors=%u\n",
        (unsigned long long)boot, (unsigned)r->session, (unsigned)r->seq, (unsigned)r->events,
        (long long)r->wall_sec, (long long)(r->up_us / 1000),
        r->have_sample ? (long long)s->wall_sec : -1LL,
        r->have_sample ? (long long)(s->up_us / 1000) : -1LL,
        r->have_sample ? (long long)((write_us - s->up_us) / 1000) : -1LL,
        (long long)(write_us / 1000), r->have_sample ? (unsigned)s->read_us : 0u, flags,
        v ? (int)s->raw_mv : -1, i ? (int)s->ma : -1,
        r->have_sample && (chg || i) ? (int)s->charging : -1,
        r->have_sample && s->have_filter && v && (chg || i) ? (int)s->charge_done : -1,
        v ? (unsigned)s->done_source : 0u, chg ? (int)s->charger_status : -1,
        v && s->have_filter ? (long)s->comp_mv : -1L, v && s->have_filter ? (long)s->ocv_mv : -1L,
        v && s->have_filter ? (long)s->curve_permille : -1L, v && s->have_filter ? (long)s->disp_permille : -1L,
        r->have_sample ? (int)s->ic_soc : -1,
        r->have_sample && s->ic_soc >= 0 ? (long long)((write_us - s->ic_us) / 1000) : -1LL,
        (int)r->locked, (long long)(r->covered_us / 1000), (long long)(r->unknown_us / 1000),
        (long long)(r->locked_us / 1000), (long long)(r->in_ma_us / 3600000),
        (long long)(r->out_ma_us / 3600000), (unsigned)r->dropped,
        (unsigned)io_errors, (unsigned)file_missed, (unsigned)file_seq,
        (unsigned)r->low_mask, r->have_sample ? (long)s->full_hold_ms : -1L,
        (unsigned)s->full_gate, (unsigned)s->restore,
        (long long)(r->stats.start_us / 1000), (unsigned)r->stats.samples,
        (unsigned)r->stats.locked_samples, (unsigned)r->stats.v_count,
        r->stats.v_count ? (long)r->stats.v_min : -1L,
        r->stats.v_count ? (long long)(r->stats.v_sum / r->stats.v_count) : -1LL,
        r->stats.v_count ? (long)r->stats.v_max : -1L, (unsigned)r->stats.i_count,
        r->stats.i_count ? (long)r->stats.i_min : -1L,
        r->stats.i_count ? (long long)(r->stats.i_sum / r->stats.i_count) : -1LL,
        r->stats.i_count ? (long)r->stats.i_max : -1L,
        (long long)(r->stats.max_gap_us / 1000), (unsigned)r->stats.cx_changes,
        r->stats.cx_changes ? (long long)(r->stats.cx_first_us / 1000) : -1LL,
        r->stats.cx_changes ? (long long)(r->stats.cx_last_us / 1000) : -1LL,
        (unsigned)r->stats.full_resets, (unsigned)r->stats.full_reset_mask, (unsigned)r->checkpoint_errors);
    return n > 0 && (size_t)n < size ? n : -1;
}

bool batt_log_append(batt_log_file_t *file, const char *path, const char *backup,
                     long limit, uint32_t session, const char *metadata, const char *line) {
    struct stat st;
    bool empty = false;
    if (stat(path, &st) != 0) {
        if (errno != ENOENT) { file->header = true; return false; }
        empty = true;
    } else if (st.st_size >= limit) {
        if (remove(backup) != 0 && errno != ENOENT) { file->header = true; return false; }
        if (rename(path, backup) != 0) { file->header = true; return false; }
        empty = true;
    } else empty = st.st_size == 0;
    FILE *f = fopen(path, "a");
    if (!f) { file->header = true; return false; }
    bool ok = true;
    if (empty || file->header || file->session != session) {
        ok = fprintf(f, "\n# battlog fmt=3 session=%u %s\n", (unsigned)session, metadata) > 0;
    }
    if (ok) ok = fputs(line, f) != EOF;
    if (fflush(f) != 0) ok = false;
    if (fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    file->header = !ok;
    if (ok) file->session = session;
    return ok;
}

bool batt_log_checkpoint_valid(const batt_log_checkpoint_t *c) {
    // 100 年上限拒绝格式损坏值，并保证累计时仍有充分的 int64 余量。
    // A 100-year bound rejects corrupt data while leaving ample int64 accumulation headroom.
    const int64_t time_limit = 100LL * 366 * 86400 * 1000000;
    const int64_t charge_limit = INT64_MAX / 4;
    return c->version == BATT_LOG_CHECKPOINT_VERSION && c->active <= 1 && c->chain && c->boot &&
        c->session && c->seq && c->wall_sec >= -1 && c->covered_us >= 0 &&
        c->covered_us < time_limit && c->unknown_us >= 0 && c->unknown_us < time_limit &&
        c->locked_us >= 0 && c->locked_us <= c->unknown_us &&
        c->in_ma_us >= 0 && c->in_ma_us < charge_limit &&
        c->out_ma_us >= 0 && c->out_ma_us < charge_limit;
}

void batt_log_checkpoint_make(batt_log_checkpoint_t *out, const batt_log_checkpoint_t *base,
                              const batt_log_record_t *r, uint64_t boot) {
    *out = *base;
    out->version = BATT_LOG_CHECKPOINT_VERSION;
    out->active = (r->events & BATT_LOG_END) == 0;
    out->boot = boot;
    out->session = r->session;
    out->seq = r->seq;
    out->wall_sec = r->wall_sec;
    out->covered_us += r->covered_us;
    out->unknown_us += r->unknown_us;
    out->locked_us += r->locked_us;
    out->in_ma_us += r->in_ma_us;
    out->out_ma_us += r->out_ma_us;
}
