# The pages against the contract, statically (P6). Both skin sync scripts call these and stop on a
# problem, so a page cannot send what the plug-in does not declare, and cannot gate on a capability
# the plug-in does not publish.
#  - commands(texts, schema): every op a page sends is a $defs/command variant, and where the
#    arguments are an object literal at the call, every argument name is one the variant declares
#    (the command schema is closed: only its arguments plus id, g and force).
#  - capabilities(schema): the capability names the contract publishes ($defs/capabilities, nested
#    "can" or the older flat booleans), so a page's {capability: controls} table plus its list of
#    informational names can be compared with them both ways.
import json
import re

ENVELOPE = {'op', 'id', 'g', 'force'}
STR = re.compile(r'"([A-Za-z]\w*)"')
CMP = re.compile(r'(?:===|!==|==|!=)\s*"[^"]*"|"[^"]*"\s*(?:===|!==|==|!=)')


def load(path):
    return json.load(open(path))


def command_table(schema):
    """op -> the argument names its variant declares"""
    out = {}
    for v in schema['$defs']['command']['oneOf']:
        out[v['properties']['op']['const']] = set(v['properties']) - ENVELOPE
    return out


def capabilities(schema):
    c = schema['$defs']['capabilities']
    can = c.get('properties', {}).get('can')
    if can:
        return set(can.get('required', [])) | set(can.get('properties', {}))
    return {k for k, v in c.get('properties', {}).items() if v.get('type') == 'boolean'}


def _match(text, i, open_c, close_c):
    """the index after the bracket that closes text[i] (strings and template literals skipped)"""
    depth, j, q = 0, i, None
    while j < len(text):
        ch = text[j]
        if q:
            if ch == '\\':
                j += 2
                continue
            if ch == q:
                q = None
        elif ch in '"\'`':
            q = ch
        elif ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    return len(text)


def _split_top(text):
    """a call's arguments (text inside the parentheses), split at top-level commas"""
    parts, depth, q, cur, j = [], 0, None, '', 0
    while j < len(text):
        ch = text[j]
        if q:
            cur += ch
            if ch == '\\' and j + 1 < len(text):
                cur += text[j + 1]
                j += 1
            elif ch == q:
                q = None
        elif ch in '"\'`':
            q = ch
            cur += ch
        elif ch in '([{':
            depth += 1
            cur += ch
        elif ch in ')]}':
            depth -= 1
            cur += ch
        elif ch == ',' and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += ch
        j += 1
    if cur.strip():
        parts.append(cur)
    return [p.strip() for p in parts]


def object_keys(lit):
    """the top-level member names of an object literal "{...}", or None when it has a spread"""
    body = _split_top(lit.strip()[1:-1])
    keys = set()
    for m in body:
        if m.startswith('...'):
            return None
        k = re.match(r'"?([A-Za-z_]\w*)"?\s*(?::|$)', m)
        if k:
            keys.add(k.group(1))
    return keys


def ops_of(expr):
    """the op names an expression can give: its string literals, not those it compares with"""
    return set(STR.findall(CMP.sub('', expr)))


def calls(text, name):
    """the argument lists of every call name(...) (name is a regex)"""
    for m in re.finditer(r'(?<![\w.])' + name + r'\(', text):
        i = m.end() - 1
        yield _split_top(text[i + 1:_match(text, i, '(', ')') - 1])


def literals_with_op(text):
    """every object literal {op: ..., ...} in the text: (op names, member names or None)"""
    for m in re.finditer(r'\{\s*op:\s*', text):
        lit = text[m.start():_match(text, m.start(), '{', '}')]
        members = _split_top(lit[1:-1])
        yield ops_of(members[0].split(':', 1)[1]), object_keys(lit)


def check_sends(sends, table, where):
    """sends: [(op names, argument names or None)] -> problems"""
    problems = []
    for ops, keys in sends:
        for op in sorted(ops):
            if op not in table:
                problems.append('%s sends op "%s", which the contract does not declare' % (where, op))
            elif keys is not None:
                extra = sorted(keys - ENVELOPE - table[op])
                if extra:
                    problems.append('%s sends %s with %s, which its contract variant does not declare' % (where, op, ', '.join(extra)))
    return problems


def check_audio(text, table, where):
    """the AUDIO / MIDI panel's audioSend({set|do: ...}) objects are audioSet arguments"""
    problems = []
    for m in re.finditer(r'\{(?:set|do):"', text):
        keys = object_keys(text[m.start():_match(text, m.start(), '{', '}')])
        extra = sorted((keys or set()) - table.get('audioSet', set()))
        if extra:
            problems.append('%s: audioSet with %s, which the contract does not declare' % (where, ', '.join(extra)))
    return problems


def check_caps(table_keys, info, schema, where):
    names = capabilities(schema)
    problems = ['%s gates on capability "%s", which the contract does not publish' % (where, x) for x in sorted((set(table_keys) | set(info)) - names)]
    problems += ['%s has no controls for capability "%s" and does not list it as informational' % (where, x) for x in sorted(names - set(table_keys) - set(info))]
    problems += ['%s lists "%s" both as gated and informational' % (where, x) for x in sorted(set(table_keys) & set(info))]
    return problems
