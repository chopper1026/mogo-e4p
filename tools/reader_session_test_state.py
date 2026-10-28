"""中文：页面测试借用真实位置类型，旧断言名称只在测试中映射到会话。
English: Page tests borrow the real position type; legacy assertion names map to the session only in tests.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def position_state(chapter=0, page=0):
    return f'''
#include "{ROOT / 'main/book/reader_position.h'}"
static struct {{ reader_position_t position; }} s_reader_session = {{
    .position = {{.chapter = {chapter}, .page = {page}}}
}};
#define s_chapter s_reader_session.position.chapter
#define s_page s_reader_session.position.page
'''
