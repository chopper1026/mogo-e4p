#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：流式转换中文维基词典和 Unihan，生成可校验的独立 TF 包。
English: Stream Chinese Wiktionary and Unihan into a verifiable standalone card package.
"""
import argparse
from functools import lru_cache
import gzip
import hashlib
from importlib.metadata import version as package_version
import json
from pathlib import Path
import sqlite3
import struct
import tempfile
import zipfile
import zlib

VERSION = 1
CONVERTER_VERSION = 2
MAX_BYTES = (511, 5631, 1023, 128)
SOURCE = '中文维基词典 · CC BY-SA 4.0 / Unihan · Unicode-3.0'
CHARACTERS = list('踽龘鬱觊觎龃龉魑魅魍魉饕餮耄耋旮旯孑孓茕踯躅踟蹰蹁跹囹圄醍醐氤氲菡萏罅隙赟翯爨纛彳亍叟匕凫黻黼鼐') + ['𠮷', '毳']
WORDS = '踽踽独行 觊觎 龃龉 魑魅魍魉 饕餮 耄耋 旮旯 孑孓 茕茕孑立 踯躅 踟蹰 蹁跹 囹圄 醍醐 氤氲 菡萏 罅隙 未雨绸缪 踌躇 蹉跎 匍匐 伛偻 邂逅 婆娑 翩跹 矍铄 佶屈聱牙 鳞次栉比 不啻 褫夺'.split()
FEEDBACK_WORDS = ['青铜', '破解']


@lru_cache(maxsize=1)
def chinese_converter():
    # 只在宿主制包时转换；设备不携带转换表或额外字体。
    # Convert at host package-build time; the device carries no conversion tables or extra fonts.
    from opencc import OpenCC
    return OpenCC('t2s')


@lru_cache(maxsize=8192)
def simplified(text):
    for _ in range(4):
        converted = chinese_converter().convert(text)
        if converted == text:
            return text
        text = converted
    raise ValueError('OpenCC simplification did not converge')


def has_chinese_gloss(definition):
    return bool(definition and not definition.startswith('Unihan 字形资料'))


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def bounded(value, limit):
    data = value.encode('utf-8')
    if len(data) <= limit:
        return value
    return data[:limit - 3].decode('utf-8', errors='ignore') + '…'


def merge(conn, key, reading='', definition='', example='', variant=''):
    if not key or len(key) > 32 or len(key.encode()) > 128 or any(ord(c) < 33 for c in key):
        return
    old = conn.execute('SELECT reading,definition,example,variant FROM entries WHERE key=?', (key,)).fetchone()
    values = [reading, definition, example, variant]
    if old:
        values = ['\n'.join(dict.fromkeys(filter(None, (a + '\n' + b).split('\n'))))
                  for a, b in zip(old, values)]
    values = [bounded(v.replace('\x00', ''), cap) for v, cap in zip(values, MAX_BYTES)]
    conn.execute('INSERT OR REPLACE INTO entries VALUES(?,?,?,?,?)', (key, *values))


def merge_chinese(conn, key, reading='', definition='', example='', variant=''):
    definition, example = simplified(definition), simplified(example)
    merge(conn, key, reading, definition, example, variant)
    alias = simplified(key)
    if alias != key:
        merge(conn, alias, reading, definition, example, variant or key)


def import_wiktionary(conn, path):
    conn.execute('CREATE TABLE IF NOT EXISTS aliases(key TEXT,target TEXT,PRIMARY KEY(key,target))')
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt', encoding='utf-8') as stream:
        for line in stream:
            row = json.loads(line)
            if row.get('lang_code') not in ('zh', 'cmn') or row.get('pos') == 'romanization':
                continue
            key = row.get('word', '')
            readings = list(dict.fromkeys(s['zh_pron'] for s in row.get('sounds', [])
                if s.get('zh_pron') and 'Pinyin' in s.get('tags', []) and 'Mandarin' in s.get('tags', [])))
            senses, examples = [], []
            for sense in row.get('senses', []):
                targets = [x.get('word', '') for kind in ('alt_of', 'form_of') for x in sense.get(kind, [])]
                if targets:
                    for target in targets:
                        for alias in dict.fromkeys((key, simplified(key))):
                            conn.execute('INSERT OR IGNORE INTO aliases VALUES(?,?)', (alias, target))
                    # 跳转义不冒充释义；全部词条导入后解引用，并阻止循环。
                    # A link is not a definition; resolve it after all entries are loaded, with a cycle bound.
                    continue
                for gloss in sense.get('glosses', []):
                    gloss = simplified(gloss)
                    if gloss and gloss not in senses:
                        senses.append(gloss)
                for example in sense.get('examples', []):
                    if example.get('text'):
                        text = simplified(example['text'])
                        if text not in examples:
                            examples.append(text)
            definition = '\n'.join(f'{i+1}. {gloss}' for i, gloss in enumerate(senses))
            merge_chinese(conn, key, ' / '.join(readings), definition, '\n'.join(examples[:2]))
            for form in row.get('forms', []):
                if any(tag in form.get('tags', []) for tag in ('Simplified-Chinese', 'Traditional-Chinese', 'alternative')):
                    alias = form.get('form', '')
                    if alias != key:
                        merge_chinese(conn, alias, ' / '.join(readings), definition, '\n'.join(examples[:2]), key)
                        conn.execute('INSERT OR IGNORE INTO aliases VALUES(?,?)', (alias, key))


def import_chinese_dictionary(conn, folder):
    # 词语是JSON数组；汉字文件是每行一个对象并带尾逗号，按上游格式读取。
    # Words use a JSON array; characters use one object per line with a trailing comma.
    word_path, character_path = folder / 'word/word.json', folder / 'character/char_detail.json'
    words = json.loads(word_path.read_text(encoding='utf-8'))
    for row in words:
        merge_chinese(conn, row['word'], row.get('pinyin', ''), row.get('explanation', ''))
    with character_path.open(encoding='utf-8') as stream:
        for line in stream:
            if not line.strip():
                continue
            row = json.loads(line.rstrip().removesuffix(','))
            readings, definitions, examples = [], [], []
            for pronunciation in row.get('pronunciations', []):
                reading = pronunciation.get('pinyin', '')
                if reading and reading not in readings:
                    readings.append(reading)
                for sense in pronunciation.get('explanations', []):
                    text = sense.get('content', '')
                    if text:
                        definitions.append((reading + '：' if reading else '') + text)
                    for example in sense.get('detail', []):
                        if example.get('text'):
                            examples.append(example['text'] + example.get('book', ''))
            merge_chinese(conn, row['char'], ' / '.join(readings), '\n'.join(definitions), '\n'.join(examples[:2]))
    return [word_path, character_path]


def resolve_aliases(conn):
    if not conn.execute("SELECT 1 FROM sqlite_master WHERE name='aliases'").fetchone():
        return 0
    resolved = 0
    for _ in range(8):
        changed = 0
        rows = conn.execute('SELECT key,target FROM aliases ORDER BY key,target').fetchall()
        for alias, target in rows:
            old = conn.execute('SELECT definition FROM entries WHERE key=?', (alias,)).fetchone()
            if old and has_chinese_gloss(old[0]):
                continue
            row = conn.execute('SELECT reading,definition,example FROM entries WHERE key=?', (target,)).fetchone()
            if row and has_chinese_gloss(row[1]):
                merge_chinese(conn, alias, *row, target)
                changed += 1
        resolved += changed
        if not changed:
            break
    return resolved


def import_unihan(conn, path):
    with zipfile.ZipFile(path) as archive:
        values = {}
        for filename in ('Unihan_Readings.txt', 'Unihan_IRGSources.txt', 'Unihan_Variants.txt'):
            with archive.open(filename) as stream:
                for raw in stream:
                    line = raw.decode('utf-8').strip()
                    if not line or line.startswith('#'):
                        continue
                    code, field, value = line.split('\t', 2)
                    if field not in ('kMandarin', 'kTotalStrokes', 'kRSUnicode', 'kTraditionalVariant', 'kSimplifiedVariant'):
                        continue
                    values.setdefault(chr(int(code[2:], 16)), {})[field] = value
        for key, row in values.items():
            old = conn.execute('SELECT definition,reading FROM entries WHERE key=?', (key,)).fetchone()
            reading = '' if old and old[1] else row.get('kMandarin', '')
            info = []
            if row.get('kTotalStrokes'):
                info.append('总笔画：' + row['kTotalStrokes'])
            if row.get('kRSUnicode'):
                info.append('部首编号/余笔：' + row['kRSUnicode'])
            definition = '' if old and old[0] else 'Unihan 字形资料；未提供中文释义\n' + '；'.join(info)
            merge(conn, key, reading, definition)


def write_package(conn, output, source=SOURCE):
    count = conn.execute('SELECT COUNT(*) FROM entries').fetchone()[0]
    if not count:
        raise ValueError('No supported Chinese entries')
    if count > 1000000 or len(source.encode('utf-8')) > 159:
        raise ValueError('Package metadata exceeds device limits')
    with tempfile.TemporaryDirectory(dir=output.parent) as folder:
        work = Path(folder)
        keys, data, index = [open(work / n, 'wb+') for n in ('keys', 'data', 'index')]
        key_at = 64 + count * 20
        data_at = key_at + conn.execute('SELECT SUM(length(CAST(key AS BLOB))) FROM entries').fetchone()[0]
        for key, *values in conn.execute('SELECT * FROM entries ORDER BY CAST(key AS BLOB)'):
            fields = [value.encode('utf-8') for value in values]
            raw = struct.pack('<4H', *(len(value) for value in fields)) + b''.join(fields)
            compressed = zlib.compress(raw, 9)
            if len(raw) > 8192 or len(compressed) > 8192:
                raise ValueError('Entry exceeds device limit: ' + key)
            encoded = key.encode('utf-8')
            index.write(struct.pack('<IIHHIHH', key_at + keys.tell(), data_at + data.tell(), len(compressed), len(raw), zlib.crc32(raw), len(encoded), 0))
            keys.write(encoded)
            data.write(compressed)
        source = source.encode('utf-8')
        meta_at = data_at + data.tell()
        header = bytearray(64)
        struct.pack_into('<8s8I', header, 0, b'MGDICT\r\n', VERSION, meta_at+len(source), count, 64, key_at, data_at, meta_at, len(source))
        struct.pack_into('<I', header, 40, zlib.crc32(source))
        struct.pack_into('<I', header, 60, zlib.crc32(header[:60]))
        temporary = work / 'chinese.mgdict'
        with temporary.open('wb') as target:
            target.write(header)
            for stream in (index, keys, data):
                stream.seek(0)
                for chunk in iter(lambda: stream.read(1024*1024), b''):
                    target.write(chunk)
                stream.close()
            target.write(source)
        temporary.replace(output)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wiktionary', type=Path, required=True, action='append')
    parser.add_argument('--unihan', type=Path)
    parser.add_argument('--chinese-dictionary', type=Path, help='mapull/chinese-dictionary checkout with word/ and character/')
    parser.add_argument('--chinese-dictionary-revision', help='Pinned commit of the supplemental Chinese dictionary')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--font', type=Path)
    parser.add_argument('--source-url', default='https://kaikki.org/zhwiktionary/漢語/')
    parser.add_argument('--dump-date', help='Wiktionary dump date from the source snapshot')
    parser.add_argument('--extraction-date', help='Kaikki extraction date from the source snapshot')
    parser.add_argument('--wiktextract-revision', help='Wiktextract commit from the source snapshot')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output.parent) as folder:
        conn = sqlite3.connect(str(Path(folder) / 'entries.sqlite'))
        conn.execute('CREATE TABLE entries(key TEXT PRIMARY KEY, reading TEXT,definition TEXT,example TEXT,variant TEXT)')
        for snapshot in args.wiktionary:
            import_wiktionary(conn, snapshot)
        supplement_paths = import_chinese_dictionary(conn, args.chinese_dictionary) if args.chinese_dictionary else []
        aliases_resolved = resolve_aliases(conn)
        # 去掉空词头；有读音但缺释义仍明确保留，Unihan随后补字形资料。
        # Drop empty headwords; retain explicit reading-only entries and supplement characters with Unihan afterward.
        empty = conn.execute("DELETE FROM entries WHERE reading='' AND definition=''").rowcount
        if args.unihan:
            import_unihan(conn, args.unihan)
        conn.commit()
        source = SOURCE if args.unihan else '中文维基词典 · CC BY-SA 4.0'
        if supplement_paths:
            source += ' / 码谱汉语词典 · MIT'
        count = write_package(conn, args.output, source)
        cmap = None
        if args.font:
            from fontTools.ttLib import TTFont
            with TTFont(args.font) as font:
                cmap = font.getBestCmap()
        samples = []
        for key in CHARACTERS[:50] + WORDS[:30] + FEEDBACK_WORDS:
            row = conn.execute('SELECT reading,definition,variant FROM entries WHERE key=?', (key,)).fetchone()
            samples.append({'query': key, 'found': bool(row), 'reading': bool(row and row[0]),
                'chinese_gloss': bool(row and has_chinese_gloss(row[1])),
                'glyphs': None if cmap is None else all(ord(c) in cmap for c in key),
                'variant_source': row[2] if row else ''})
        manifest = {'format': VERSION, 'converter': f'tools/build_dictionary.py:{CONVERTER_VERSION}', 'entries': count,
            'package_sha256': digest(args.output), 'package_bytes': args.output.stat().st_size,
            'source_url': args.source_url, 'wiktionary_dump': args.dump_date, 'extraction': args.extraction_date,
            'wiktextract': args.wiktextract_revision, 'source_sha256': {str(p): digest(p) for p in args.wiktionary},
            'licenses': ['CC-BY-SA-4.0', 'Unicode-3.0'] if args.unihan else ['CC-BY-SA-4.0'],
            'unihan_sha256': digest(args.unihan) if args.unihan else None,
            'font_sha256': digest(args.font) if args.font else None, 'samples': samples}
        manifest.update({'simplification': {'engine': 'opencc-python-reimplemented',
                'version': package_version('opencc-python-reimplemented'), 'config': 't2s'},
            'chinese_dictionary': {'url': 'https://github.com/mapull/chinese-dictionary',
                'revision': args.chinese_dictionary_revision, 'license': 'MIT',
                'source_sha256': {str(p): digest(p) for p in supplement_paths}} if supplement_paths else None,
            'aliases_resolved': aliases_resolved, 'empty_headwords_removed': empty,
            'coverage': {'headwords_with_chinese_gloss': sum(has_chinese_gloss(r[0]) for r in conn.execute('SELECT definition FROM entries')),
                'multi_character_with_chinese_gloss': sum(len(k)>1 and has_chinese_gloss(d) for k,d in conn.execute('SELECT key,definition FROM entries'))}})
        if supplement_paths:
            manifest['licenses'].append('MIT')
        args.output.with_suffix('.manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
        args.output.with_suffix('.LICENSE.txt').write_text(
            '中文维基词典 / Chinese Wiktionary contributors\n' + args.source_url + '\n'
            'Extracted by Wiktextract / Kaikki.org; filtered and merged for mogo.\n'
            'Definitions/examples simplified with OpenCC t2s; original headwords and simplified aliases retained.\n'
            'CC BY-SA 4.0: https://creativecommons.org/licenses/by-sa/4.0/\n'
            'Individual page attribution/history: https://zh.wiktionary.org/wiki/<headword>?action=history\n'
            'The share-alike license applies to dictionary data, not the separate firmware.\n'
            + ('Unihan: Copyright © Unicode, Inc. https://www.unicode.org/license.txt\n' if args.unihan else '') +
            ('Supplemental definitions: 码谱 / mapull, MIT License\nhttps://github.com/mapull/chinese-dictionary\n'
             'Revision: ' + (args.chinese_dictionary_revision or 'unspecified') + '\n' if supplement_paths else '') +
            'Redistribute this notice, the manifests, and the complete applicable license texts.\n', encoding='utf-8')
        print(json.dumps({'entries': count, 'bytes': manifest['package_bytes'], 'sha256': manifest['package_sha256'],
            'samples': len(samples), 'chinese_gloss': sum(s['chinese_gloss'] for s in samples),
            'reading': sum(s['reading'] for s in samples), 'glyphs': sum(bool(s['glyphs']) for s in samples)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
