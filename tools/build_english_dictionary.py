#!/usr/bin/env python3
"""Build the offline EN-ZH/EN-EN package from pinned Wiktextract JSONL inputs."""
import argparse
from collections import defaultdict
import gzip
import json
from itertools import groupby
from pathlib import Path
import re
import shutil
import sqlite3
import sys
import tempfile
from urllib.parse import quote
import zipfile
import zlib

from build_dictionary import MAX_BYTES, VERSION, bounded, digest, write_package

CONVERTER_VERSION = 2
MAX_HEADWORDS = 1_000_000
SRC = {'en': 'English Wiktionary', 'zh': 'Chinese Wiktionary'}
LICENSE_URL = 'https://creativecommons.org/licenses/by-sa/4.0/legalcode.txt'
HAN = re.compile(r'[\u3400-\u9fff\uf900-\ufaff\U00020000-\U000323af]')
POS = {
    'adj': ('adjective', '形容詞'), 'adv': ('adverb', '副詞'), 'noun': ('noun', '名詞'),
    'verb': ('verb', '動詞'), 'pron': ('pronoun', '代詞'), 'intj': ('interjection', '感嘆詞'),
    'name': ('proper noun', '專有名詞'), 'prep': ('preposition', '介詞'),
    'conj': ('conjunction', '連詞'), 'contraction': ('contraction', '縮約形'),
    'abbrev': ('abbreviation', '縮寫'), 'phrase': ('phrase', '短語'), 'proverb': ('proverb', '諺語'), 'det': ('determiner', '限定詞'),
}


def english_letter(c):
    return 'a' <= c <= 'z' or 'A' <= c <= 'Z' or 'À' <= c <= 'Ö' or 'Ø' <= c <= 'ö' or 'ø' <= c <= 'ÿ'


def english_alnum(c):
    return english_letter(c) or '0' <= c <= '9'


def space(c):
    return c <= ' ' or c in '\u0085\u00a0\u1680\u2028\u2029\u202f\u205f\u3000' or '\u2000' <= c <= '\u200a'


JOINERS = {**dict.fromkeys("'‘’ʼ＇", "'"), **dict.fromkeys('-‐‑－', '-')}


def edge(c):
    return (space(c) or c in JOINERS or '!' <= c <= '~' and not english_alnum(c) or c in '«»…、。！，？'
            or '\u2012' <= c <= '\u201f' or '\u3008' <= c <= '\u3011')


def english_key(value):
    # 与 reader_selection.c 保持一致；ASCII 和 Latin-1，不依赖 Unicode 版本隐式变换。
    # Match reader_selection.c: ASCII and Latin-1, no implicit Unicode-version transforms.
    if '\0' in value or len(value.encode()) > 128:
        return ''
    start, end = 0, len(value)
    while start < end and edge(value[start]):
        start += 1
    while end > start and edge(value[end - 1]):
        end -= 1
    value = value[start:end]
    out = []
    for i, c in enumerate(value):
        if english_alnum(c):
            out.append(c.lower())
        elif space(c):
            if out[-1] != ' ':
                out.append(' ')
        elif c in JOINERS and i > 0 and i + 1 < len(value) and english_alnum(value[i - 1]) and english_alnum(value[i + 1]):
            out.append(JOINERS[c])
        else:
            return ''
    return ''.join(out) if len(out) <= 32 and any(map(english_letter, out)) else ''


def clean(value):
    return ' '.join(value.replace('\0', '').split()) if isinstance(value, str) else ''


def label(pos, value, chinese=False):
    name = POS.get(pos, ('', ''))[chinese]
    return f'[{name}] {value}' if name else value


def lines(old, new, limit):
    return bounded('\n'.join(dict.fromkeys(x for x in [*old.split('\n'), *new] if x)), limit)


def add_entry(db, mode, priority, key, pos, word, edition, rank, reading, gloss, examples):
    if not gloss:
        return
    old = db.execute('SELECT rank, reading, definition, example FROM direct WHERE mode=? AND priority=? AND key=? AND pos=?',
                     (mode, priority, key, pos)).fetchone()
    fields = [reading, gloss, examples]
    if old:
        if rank < old[0]:
            fields = [lines('', new + previous.split('\n'), cap) for previous, new, cap in zip(old[1:], fields, MAX_BYTES)]
        else:
            fields = [lines(previous, new, cap) for previous, new, cap in zip(old[1:], fields, MAX_BYTES)]
        rank = min(old[0], rank)
    else:
        fields = [lines('', new, cap) for new, cap in zip(fields, MAX_BYTES)]
    db.execute('INSERT OR REPLACE INTO direct VALUES (?,?,?,?,?,?,?,?)', (mode, priority, key, pos, rank, *fields))
    db.execute('INSERT OR IGNORE INTO sources VALUES (?,?,?,?,?,?)', (mode, priority, key, pos, edition, word))


def mandarin_translations(grouped, translations):
    for tr in translations:
        code = tr.get('code') or tr.get('lang_code')
        name = tr.get('lang')
        # Wiktextract 也用 zh + 明确的 Chinese Mandarin 标签；排除无标注中文和其它方言。
        # Wiktextract also uses zh with an explicit Mandarin label; exclude generic Chinese and other varieties.
        word = clean(tr.get('word'))
        if (code == 'cmn' or code == 'zh' and (name in ('Mandarin', 'Chinese Mandarin') or 'Mandarin' in tr.get('tags', []))) and HAN.search(word):
            grouped[clean(tr.get('sense'))].append(word)


def ordered_translations(translations, glosses):
    # 保留源词义顺序；Wiktextract 消歧后同组译词可能散落在不同义项中。
    # Keep source sense order even when Wiktextract attached translations to different senses.
    indexed = [set(re.findall(r'[a-z]{3,}', gloss.lower())) for gloss in glosses]

    def order(sense):
        terms = set(re.findall(r'[a-z]{3,}', sense.lower()))
        score, position = min(((-len(terms & value), i) for i, value in enumerate(indexed)), default=(0, 0))
        return position if score < 0 else len(indexed)

    return ['、'.join(dict.fromkeys(translations[sense])) for sense in sorted(translations, key=order)]


def import_source(db, path, edition):
    open_text = gzip.open if path.suffix == '.gz' else open
    with open_text(path, 'rt', encoding='utf-8') as stream:
        for i, row in enumerate(stream, 1):
            if i % 200000 == 0:
                print(f'{edition}: {i:,} records read', flush=True)
            entry = json.loads(row)
            if entry.get('lang_code') != 'en':
                continue
            word = entry['word']
            key = english_key(word)
            if not key:
                continue
            pos = entry.get('pos', '')
            reading = [ipa for item in entry.get('sounds', []) if (ipa := clean(item.get('ipa')))]
            gloss, secondary, examples, translations = [], [], [], defaultdict(list)
            rank = i if word == key else i + (1 << 40)
            for sense in entry.get('senses', []):
                # 只取本版 Wiktionary 的结构化词义；不采集引文、外站或词库扩展数据。
                # Take attributed Wiktionary senses only; ignore quotations, external enrichments and thesaurus data.
                if not sense.get('id', '').startswith(edition + '-'):
                    continue
                roots = sense.get('form_of') or []
                for form in roots:
                    target = english_key(form.get('word', ''))
                    if target and target != key:
                        db.execute('INSERT OR IGNORE INTO forms VALUES (?,?,?,?)', (key, target, edition, word))
                if roots or 'no-gloss' in sense.get('tags', []):
                    continue
                values = sense.get('glosses', [])
                value = clean(values[-1]) if values else ''
                if value and not any(mark in value for mark in ('====', '{{', '}}')) and (edition == 'en' or HAN.search(value)):
                    uncommon = {'archaic', 'obsolete', 'rare', 'dialectal', 'nonstandard', 'dated'} & set(sense.get('tags', []))
                    (secondary if uncommon else gloss).append(value)
                if edition == 'en':
                    for sample in sense.get('examples', []):
                        if sample.get('type') == 'example' and not sample.get('ref') and (text := clean(sample.get('text'))):
                            examples.append(text)
                    mandarin_translations(translations, sense.get('translations', []))
            gloss += secondary
            if edition == 'en':
                # 旧结构将译词放在英语条目上，而不是具体义项。/ Older extracts keep translations on the English entry.
                if gloss:
                    mandarin_translations(translations, entry.get('translations', []))
                groups = ordered_translations(translations, gloss)
                add_entry(db, 'en', 0, key, pos, word, edition, rank, reading, [label(pos, x) for x in gloss], examples)
                add_entry(db, 'zh', 1, key, pos, word, edition, rank, reading, [label(pos, x, True) for x in groups], [])
            else:
                add_entry(db, 'zh', 0, key, pos, word, edition, rank, reading, [label(pos, x, True) for x in gloss], [])
    db.commit()


def write_mode(db, output, mode):
    db.executescript('DROP TABLE IF EXISTS entries; DROP TABLE IF EXISTS aliases;'
                     'CREATE TABLE entries (key TEXT PRIMARY KEY, reading TEXT, definition TEXT, example TEXT, variant TEXT);'
                     'CREATE TABLE aliases (key TEXT PRIMARY KEY, target TEXT);'
                     'CREATE INDEX aliases_target ON aliases(target, key);')
    # 同词性优先用中文维基释义，只有该词性缺项才加入普通话译词。
    # Chinese glosses win for each part of speech; fill missing groups from explicit Mandarin translations.
    db.execute('DROP TABLE IF EXISTS choices')
    db.execute('CREATE TABLE choices (key TEXT, pos TEXT, priority INT, PRIMARY KEY(key,pos)) WITHOUT ROWID')
    db.execute('INSERT INTO choices SELECT key,pos,min(priority) FROM direct WHERE mode=? GROUP BY key,pos', (mode,))
    selected = db.execute('''SELECT d.key, d.reading, d.definition, d.example FROM direct d JOIN choices c
        ON d.key=c.key AND d.pos=c.pos AND d.priority=c.priority
        LEFT JOIN direct e ON e.mode='en' AND e.key=d.key AND e.pos=d.pos AND e.priority=0
        WHERE d.mode=? ORDER BY d.key, d.pos IN ('name','unknown','prefix','suffix','symbol'),
        (instr(d.key, char(39)) > 0 OR instr(d.key, ' ') > 0) AND d.pos != 'verb',
        coalesce(e.rank,d.rank),d.pos''', (mode,))
    for key, group in groupby(selected, key=lambda row: row[0]):
        fields = list(zip(*(row[1:] for row in group)))
        values = [lines('', [part for value in field for part in value.split('\n')], cap) for field, cap in zip(fields, MAX_BYTES)]
        db.execute('INSERT INTO entries VALUES (?,?,?,?,?)', (key, *values, ''))
    direct = db.execute('SELECT count(*) FROM entries').fetchone()[0]
    if not 0 < direct <= MAX_HEADWORDS:
        raise ValueError(f'{mode}: {direct} direct keys exceed the supported package range')
    db.execute('DROP TABLE IF EXISTS candidate_aliases')
    db.execute('''CREATE TABLE candidate_aliases AS
        SELECT f.key AS key, min(f.target) AS target FROM forms f JOIN entries e ON e.key=f.target
        WHERE NOT EXISTS (SELECT 1 FROM entries WHERE key=f.key)
        GROUP BY f.key HAVING count(DISTINCT f.target)=1''')
    # 词条上限内保留所有直接释义；别名优先保留两种释义均可用的常见短词形。
    # Retain all direct entries; within the format cap prefer aliases whose lemmas have both gloss modes, then shorter forms.
    db.execute('''INSERT INTO aliases SELECT key,target FROM candidate_aliases a
        ORDER BY EXISTS (SELECT 1 FROM direct WHERE mode=? AND key=a.target) DESC, length(a.key), a.key
        LIMIT ?''', ('zh' if mode == 'en' else 'en', MAX_HEADWORDS - direct))
    candidates = db.execute('SELECT count(*) FROM candidate_aliases').fetchone()[0]
    db.execute('''INSERT INTO entries
        SELECT a.key, e.reading, e.definition, e.example, a.target FROM aliases a JOIN entries e ON e.key=a.target''')
    db.commit()
    print(f'{mode}: writing MGDICT and contributor attributions', flush=True)
    count = write_package(db, output / f'english-{mode}.mgdict', 'Wiktionary / CC BY-SA 4.0')
    evidence = output / f'english-{mode}.attribution.tsv'
    with evidence.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('key\tWiktionary page and contributor history\n')
        for key, edition, word in db.execute('''
            WITH used AS (SELECT s.* FROM sources s JOIN choices c
              ON s.key=c.key AND s.pos=c.pos AND s.priority=c.priority WHERE mode=?)
            SELECT key, edition, word FROM used
            UNION SELECT a.key, u.edition, u.word FROM aliases a JOIN used u ON u.key=a.target
            UNION SELECT f.key, f.edition, f.word FROM forms f JOIN aliases a ON a.key=f.key AND a.target=f.target
            ORDER BY 1, 2, 3''', (mode,)):
            stream.write(f"{key}\thttps://{edition}.wiktionary.org/wiki/{quote(word.replace(' ', '_'), safe='')}\n")
    aliases = db.execute('SELECT count(*) FROM aliases').fetchone()[0]
    return {'headwords': count, 'direct': direct, 'aliases': aliases, 'aliases_omitted_at_format_cap': candidates - aliases}


def archive(work, target):
    sums = work / 'SHA256SUMS'
    sums.write_text(''.join(f'{digest(p)}  {p.relative_to(work).as_posix()}\n' for p in sorted(work.rglob('*')) if p.is_file()),
                    encoding='utf-8')
    with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED) as package:
        for path in sorted(work.rglob('*')):
            if path.is_file():
                info = zipfile.ZipInfo(path.relative_to(work).as_posix(), (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                with path.open('rb') as source, package.open(info, 'w') as dest:
                    shutil.copyfileobj(source, dest)


def build(lock, input_dir, target):
    inputs = {}
    for name in ('en', 'zh', 'license'):
        pin = lock['inputs'][name]
        path = input_dir / pin['file']
        if digest(path) != pin['sha256']:
            raise ValueError(f'SHA-256 mismatch for {path}')
        inputs[name] = path
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent, prefix='english-') as folder:
        temp = Path(folder)
        work = temp / 'package'
        output = work / 'dictionaries'
        (output / 'licenses').mkdir(parents=True)
        shutil.copyfile(inputs['license'], output / 'licenses/CC-BY-SA-4.0.txt')
        db = sqlite3.connect(temp / 'dictionary.sqlite3')
        try:
            db.executescript('''CREATE TABLE direct (mode TEXT, priority INT, key TEXT, pos TEXT, rank INT, reading TEXT, definition TEXT,
                example TEXT, PRIMARY KEY(mode, key, pos, priority)) WITHOUT ROWID;
                CREATE TABLE forms (key TEXT, target TEXT, edition TEXT, word TEXT, PRIMARY KEY(key, target, edition, word)) WITHOUT ROWID;
                CREATE TABLE sources (mode TEXT, priority INT, key TEXT, pos TEXT, edition TEXT, word TEXT,
                    PRIMARY KEY(mode, key, pos, priority, edition, word)) WITHOUT ROWID;''')
            import_source(db, inputs['zh'], 'zh')
            import_source(db, inputs['en'], 'en')
            counts = {mode: write_mode(db, output, mode) for mode in ('zh', 'en')}
        finally:
            db.close()
        manifest = {'format': VERSION, 'converter': CONVERTER_VERSION, 'snapshot': lock, 'entries': counts,
                    'python': sys.version.split()[0], 'zlib': zlib.ZLIB_RUNTIME_VERSION, 'license': 'CC-BY-SA-4.0'}
        (output / 'english.manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        (output / 'english.LICENSE.txt').write_text(
            f"Adapted from {SRC['en']} and {SRC['zh']} contributors via Kaikki.org/Wiktextract.\n"
            'English glosses and attributed usage examples come from English Wiktionary; Chinese glosses from Chinese\n'
            'Wiktionary take precedence over explicitly identified Mandarin translations from English Wiktionary.\n'
            'Changes: selected and regrouped fields, normalized headwords, retained explicit inflection mappings,\n'
            'omitted external quotations, deduplicated, and truncated entries to fit device limits.\n'
            'Per-headword source pages and their edit histories identify the contributors (see *.attribution.tsv).\n'
            f'Adapted dictionary data is licensed CC BY-SA 4.0: {LICENSE_URL}\n'
            'Complete license: licenses/CC-BY-SA-4.0.txt. The firmware has a separate license.\n', encoding='utf-8')
        (work / 'README.txt').write_text(
            'mogo 英文离线词库（英汉 / 英英）\n退出阅读后，将 dictionaries 内的文件合并复制到 TF 卡根目录的 dictionaries。\n'
            '已有中文词库不需要替换；合并而不是删除目录。安全弹出存储卡或退出 USB 读卡模式。\n'
            '须使用支持英文查词的固件；在 TXT、EPUB 或支持的传统 MOBI 正文长按英文单词，点英汉或英英。\n'
            '英汉保留来源的简体及繁体字形；阅读字体缺字时请更换支持汉字的字体。\n'
            '保留来源和许可文件，可用 SHA256SUMS 检查完整性。\n\n'
            'Merge the dictionaries directory into the root of the TF card without deleting the Chinese dictionary.\n'
            'Keep the attribution files, source manifest and full license. Use SHA256SUMS to check the files.\n', encoding='utf-8')
        archive(work, temp / 'output.zip')
        (temp / 'output.zip').replace(target)
    print(f'{target}: {counts}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lock', type=Path, default=Path(__file__).with_name('english-dictionary-sources.json'))
    parser.add_argument('--input-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Path to the output ZIP')
    args = parser.parse_args()
    build(json.loads(args.lock.read_text(encoding='utf-8')), args.input_dir, args.output)


if __name__ == '__main__':
    main()
