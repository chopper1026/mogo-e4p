/*
 * 中文：集中章节解析结果的交接与回收；现有书源分派仍保持单实例。
 * English: Centralize chapter transfer and cleanup while retaining the singleton source dispatcher.
 */
#include "reader_session.h"
#include "book_source.h"

static reader_session_t *s_source_owner;

esp_err_t reader_session_open(reader_session_t *session, const char *path) {
    if (!session || !path || !path[0]) return ESP_ERR_INVALID_ARG;
    if (s_source_owner) return ESP_ERR_INVALID_STATE;
    esp_err_t result = book_open(path);
    if (result == ESP_OK) s_source_owner = session;
    return result;
}

bool reader_session_has_source(const reader_session_t *session) {
    return session && s_source_owner == session;
}

const html_text_t *reader_session_content(const reader_session_t *session) {
    return &session->content;
}

void reader_session_adopt_chapter(reader_session_t *session, html_text_t *next) {
    if (next == &session->content) return;
    html_text_free(&session->content);
    session->content = *next;
    *next = (html_text_t){0};
}

void reader_session_release_chapter(reader_session_t *session) {
    html_text_free(&session->content);
}

void reader_session_close(reader_session_t *session) {
    if (!session) return;
    reader_session_release_chapter(session);
    if (s_source_owner == session) {
        book_close();
        s_source_owner = NULL;
    }
}
