#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：以小型数据核对简体释义、未标别名、空条目补全和跳转循环。
English: Verify simplified definitions, unmarked aliases, supplemental glosses and redirect cycles.
"""
import json
from pathlib import Path
import sqlite3
import tempfile
import build_dictionary as builder


with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    db = sqlite3.connect(':memory:')
    db.execute('CREATE TABLE entries(key TEXT PRIMARY KEY,reading TEXT,definition TEXT,example TEXT,variant TEXT)')
    snapshot = root / 'words.jsonl'
    rows = [
        {'word': '青銅', 'lang_code': 'zh', 'senses': [{'glosses': ['銅與錫的合金'],
            'examples': [{'text': '這件青銅器。'}, {'text': '这件青铜器。'}]}]},
        {'word': '破解', 'lang_code': 'zh', 'senses': [{'tags': ['no-gloss']}],
            'sounds': [{'zh_pron': 'pòjiě', 'tags': ['Mandarin', 'Pinyin']}]},
        {'word': '甲别', 'lang_code': 'zh', 'senses': [{'glosses': ['乙别的异体'], 'alt_of': [{'word': '乙别'}]}]},
        {'word': '乙别', 'lang_code': 'zh', 'senses': [{'form_of': [{'word': '青銅'}]}]},
        {'word': '环甲', 'lang_code': 'zh', 'senses': [{'form_of': [{'word': '环乙'}]}]},
        {'word': '环乙', 'lang_code': 'zh', 'senses': [{'form_of': [{'word': '环甲'}]}]},
        {'word': '乾燥', 'lang_code': 'zh', 'senses': [{'glosses': ['這種乾燥的狀態。']}]},
    ]
    snapshot.write_text(''.join(json.dumps(row, ensure_ascii=False) + '\n' for row in rows))
    builder.import_wiktionary(db, snapshot)
    get = lambda key: db.execute('SELECT reading,definition,example,variant FROM entries WHERE key=?', (key,)).fetchone()
    assert get('青铜')[1:3] == ('1. 铜与锡的合金', '这件青铜器。')
    assert get('青銅')[1] == get('青铜')[1] and get('青铜')[3] == '青銅'
    assert get('干燥')[1] == '1. 这种干燥的状态。' and get('乾燥')
    raw = '〔于朁〕又作“於潜”，古縣名。'
    once = builder.simplified(raw)
    assert once == builder.chinese_converter().convert(once)
    builder.merge_chinese(db, '青铜', definition='1. 銅與錫的合金')
    assert get('青铜')[1].count('铜与锡的合金') == 1
    assert not get('破解')[1] and not builder.has_chinese_gloss(get('破解')[1])
    assert builder.resolve_aliases(db) == 2
    assert get('甲别')[1] == get('青铜')[1] and not get('环甲')[1]
    supplement = root / 'supplement'
    (supplement / 'word').mkdir(parents=True)
    (supplement / 'character').mkdir()
    (supplement / 'word/word.json').write_text(json.dumps([
        {'word': '破解', 'pinyin': 'pò jiě', 'explanation': '解開難題。'},
        {'word': '青铜', 'pinyin': 'qīng tóng', 'explanation': '詞庫補充樣本。'}]))
    character = {'char': '踽', 'pronunciations': [{'pinyin': 'jǔ', 'explanations': [
        {'content': '獨行的樣子。', 'detail': [{'text': '獨行樣本。', 'book': '測試'}]}]}]}
    (supplement / 'character/char_detail.json').write_text(json.dumps(character) + ',\n')
    builder.import_chinese_dictionary(db, supplement)
    assert '解开难题。' in get('破解')[1]
    assert '词库补充样本。' in get('青铜')[1] and '铜与锡的合金' in get('青铜')[1]
    assert get('踽')[:3] == ('jǔ', 'jǔ：独行的样子。', '独行样本。测试')
    assert not builder.has_chinese_gloss('Unihan 字形资料；未提供中文释义')
    package = root / 'test.mgdict'
    builder.write_package(db, package)
    assert package.read_bytes()[:8] == b'MGDICT\r\n'
    print('PASS: simplified definitions/examples, unmarked aliases, empty-gloss supplementation, redirect chains/cycles and original keys')
