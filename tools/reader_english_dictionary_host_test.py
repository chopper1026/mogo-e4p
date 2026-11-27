#!/usr/bin/env python3
"""Verify reproducible English archives through the actual bounded firmware reader."""
import argparse
from pathlib import Path
import json
import subprocess
import tempfile
import zipfile

from build_english_dictionary import HAN, build, digest, english_key

ROOT = Path(__file__).resolve().parents[1]
READER = ROOT / 'build/dictionary-tests/dictionary'


def check_zip(path, dest):
    with zipfile.ZipFile(path) as package:
        names = set(package.namelist())
        assert {f'dictionaries/english-{mode}.{ext}' for mode in ('zh', 'en')
                for ext in ('attribution.tsv', 'mgdict')} <= names
        assert {'SHA256SUMS', 'README.txt', 'dictionaries/english.manifest.json',
                'dictionaries/english.LICENSE.txt', 'dictionaries/licenses/CC-BY-SA-4.0.txt'} <= names
        assert all(not name.startswith('/') and '..' not in Path(name).parts for name in names)
        assert len(names) == len(package.namelist())
        package.extractall(dest)
    checked = set()
    for line in (dest / 'SHA256SUMS').read_text().splitlines():
        sha, name = line.split('  ', 1)
        assert name in names and digest(dest / name) == sha, name
        checked.add(name)
    assert checked == names - {'SHA256SUMS'}
    return dest / 'dictionaries'


def lookup(directory, word, mode, found=True):
    result = subprocess.run([str(READER), 'english', str(directory / f'english-{mode}.mgdict'), word],
                            capture_output=True, text=True, timeout=5)
    if not found:
        assert result.returncode == 3, (word, mode, result.returncode, result.stderr)
        return ''
    assert result.returncode == 0 and result.stdout.strip(), (word, mode, result.returncode, result.stderr)
    return result.stdout


def entry(word, pos, edition, gloss, **sense):
    return {'lang_code': 'en', 'word': word, 'pos': pos,
            'senses': [{'id': f'{edition}-{word}-{pos}', 'glosses': [gloss], **sense}]}


def fixture(output):
    for raw, expected in [('《 Don’t！ 》', "don't"), (' Take　\nOFF！ ', 'take off'),
                          ('Élan  well‑being', 'élan well-being'), ('123', ''), ('中hello', ''), ('take…off', '')]:
        assert english_key(raw) == expected
    sources = {
        'zh': [entry('book', 'noun', 'zh', '書籍'), entry("don't", 'abbrev', 'zh', '表示否定'),
               entry('running', 'verb', 'zh', 'run 的現在分詞', form_of=[{'word': 'run'}]),
               entry('run', 'verb', 'zh', '跑步'), entry('take off', 'verb', 'zh', '起飛')],
        'en': [entry('book', 'noun', 'en', 'A collection of pages.', translations=[{'code': 'cmn', 'word': '書'}]),
               entry('book', 'verb', 'en', 'To reserve.', translations=[{'code': 'zh', 'lang': 'Chinese Mandarin', 'word': '預訂'},
                                                                       {'code': 'zh', 'word': '甲'}, {'code': 'yue', 'word': '乙'}]),
               entry("don't", 'abbrev', 'en', 'A contraction of do not.'),
               entry('run', 'verb', 'en', 'Move swiftly on foot.', examples=[{'type': 'quotation', 'text': 'Excluded quotation'}]),
               entry('running', 'verb', 'en', 'Present participle of run.', form_of=[{'word': 'run'}]),
               entry('running', 'noun', 'en', 'The activity of running.'),
               {'lang_code': 'en', 'word': 'hello', 'pos': 'intj', 'senses': [
                   {'id': 'en-hello-1', 'glosses': ['A greeting (salutation).'], 'translations': [
                       {'lang': 'Chinese Mandarin', 'code': 'zh', 'word': '喂', 'sense': 'when answering the telephone'},
                       {'lang': 'Chinese Mandarin', 'code': 'zh', 'word': '你好', 'sense': 'greeting'}]},
                   {'id': 'en-hello-2', 'glosses': ['A greeting used when answering the telephone.']}]},
               entry('take off', 'verb', 'en', 'To leave the ground.'),
               entry('walked', 'verb', 'en', 'Past tense of walk.', form_of=[{'word': 'walk'}])],
    }
    pins = {}
    for edition, entries in sources.items():
        path = output / f'{edition}.jsonl'
        path.write_text(''.join(json.dumps(row) + '\n' for row in entries), encoding='utf-8')
        pins[edition] = {'file': path.name, 'sha256': digest(path)}
    license_file = output / 'license.txt'
    license_file.write_text('Fixture stands in for the supplied full CC BY-SA license.\n')
    pins['license'] = {'file': license_file.name, 'sha256': digest(license_file)}
    lock = {'inputs': pins}
    first, second = output / 'one.zip', output / 'two.zip'
    build(lock, output, first)
    build(lock, output, second)
    assert digest(first) == digest(second)
    directory = check_zip(first, output / 'extracted')
    reading = lookup(directory, '“BOOK!”', 'zh')
    assert '書籍' in reading and '預訂' in reading and '\n[名詞] 書\n' not in reading and '甲' not in reading and '乙' not in reading
    assert 'A collection' in lookup(directory, 'Book', 'en')
    assert lookup(directory, 'Hello', 'zh').splitlines()[2:] == ['[感嘆詞] 你好', '[感嘆詞] 喂', '']
    assert lookup(directory, 'Running', 'zh').splitlines()[1] == 'run'
    assert lookup(directory, 'Running', 'en').splitlines()[1:3] == ['', '[noun] The activity of running.']
    assert 'Excluded quotation' not in lookup(directory, 'run', 'en')
    assert '起飛' in lookup(directory, ' Take　OFF! ', 'zh')
    assert 'contraction' in lookup(directory, 'Don’t', 'en')
    lookup(directory, 'walked', 'en', found=False)
    attr = (directory / 'english-zh.attribution.tsv').read_text()
    assert 'running\thttps://zh.wiktionary.org/wiki/run\n' in attr
    assert 'running\thttps://zh.wiktionary.org/wiki/running\n' in attr
    try:
        build({'inputs': {**pins, 'en': {**pins['en'], 'sha256': '0' * 64}}}, output, second)
    except ValueError as error:
        assert 'SHA-256 mismatch' in str(error)
    else:
        raise AssertionError('incorrect source digest accepted')
    print('PASS: real firmware lookup, normalization parity, translation priorities, aliases, sources and deterministic ZIP')


def actual(path, output):
    directory = check_zip(path, output)
    keys = ('hello', 'book', 'read', 'run', 'running', 'Don’t', 'take off')
    en_hints = ('greeting', 'sheets', 'written', 'move', 'running', 'do not', 'remove')
    zh_hints = ('你好', '書', '讀', '跑', '跑', '不要', '起飛')
    for mode in ('zh', 'en'):
        for key, hint in zip(keys, zh_hints if mode == 'zh' else en_hints):
            entry_text = lookup(directory, key, mode)
            assert hint in entry_text.lower(), (mode, key, hint)
            lines = entry_text.splitlines()
            assert len(lines) >= 3 and lines[2]
            assert (mode == 'en' or HAN.search('\n'.join(lines[2:]))), (mode, key)
            print(mode, key, '→', lines[1] or lines[0], ':', lines[2][:120])
        alias = lookup(directory, 'walked', mode).splitlines()
        assert alias[1] == 'walk', (mode, alias)
    print('PASS: both archived packages matched all sample keys and explicit walked → walk')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temp:
        if args.archive:
            actual(args.archive, Path(temp))
        else:
            fixture(Path(temp))
