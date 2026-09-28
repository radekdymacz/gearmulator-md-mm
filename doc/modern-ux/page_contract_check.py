# The pages against the contract, statically (P6). Both skin sync scripts call these and stop on a
# problem, so a page cannot send what the plug-in does not declare, and cannot gate on a capability
# the plug-in does not publish.
#  - commands(texts, schema): every op a page sends is a $defs/command variant, and where the
#    arguments are an object literal at the call, every argument name is one the variant declares
#    (the command schema is closed: only its arguments plus id, g and force).
#  - capabilities(schema): the capability names the contract publishes ($defs/capabilities, nested
#    "can" or the older flat booleans), so a page's {capability: controls} table plus its list of
#    informational names can be compared with them both ways.
#  - lifecycle_values / check_lifecycle: a page's LIFE map (machine.lifecycle -> the page's words)
#    against $defs/lifecycle's enum, both ways: a key LIFE has that the contract does not, or a
#    lifecycle value LIFE does not map, are both a page/contract mismatch.
#  - message_types / check_message_types: the message "type" values a page's Bridge.onMessage
#    handles (switch (m.type) { case "x": } and m.type === "x") against $defs/message's oneOf
#    branches, both ways: a type the page handles that the contract does not send, or a type the
#    contract sends that nothing on the page handles, are both worth flagging.
#  - validate(instance, def_name, schema): a small draft-07-ish subset (type, enum, const,
#    required, properties, additionalProperties, items, minItems/maxItems, minimum/maximum,
#    oneOf, allOf with if/then/else, $ref) -- run standalone (python3 page_contract_check.py
#    <schema.json> <defName> < fixture.json) so a page test's own fixtures (node) can be checked
#    against the same contract the page is checked against, without a schema library dependency.
import json
import re
import sys

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


def lifecycle_values(schema):
    return set(schema['$defs']['lifecycle']['enum'])


def message_types(schema):
    """the "type" a message can be: the message def's oneOf branches, each a const"""
    msg = schema['$defs']['message']
    return {b['properties']['type']['const'] for b in msg['oneOf'] if 'const' in b.get('properties', {}).get('type', {})}


def find_object(text, name):
    """the literal text of `name = {...}` (bare or const/let), or None"""
    m = re.search(r'\b' + re.escape(name) + r'\s*=\s*\{', text)
    if not m:
        return None
    i = m.end() - 1
    return text[i:_match(text, i, '{', '}')]


def message_types_handled(text):
    """the message "type" values a page's onMessage handles: m.type === "x", and case "x": inside
    a switch (m.type) block (a case elsewhere, e.g. a different switch, is not one of these)"""
    types = set(re.findall(r'm\.type\s*===\s*"([A-Za-z]+)"', text))
    for sw in re.finditer(r'switch\s*\(\s*m\.type\s*\)\s*\{', text):
        i = sw.end() - 1
        block = text[i:_match(text, i, '{', '}')]
        types |= set(re.findall(r'case\s*"([A-Za-z]+)"\s*:', block))
    return types


def check_lifecycle(life_keys, schema, where):
    names = lifecycle_values(schema)
    problems = ['%s: LIFE has "%s", which the contract\'s lifecycle does not declare' % (where, x) for x in sorted(set(life_keys) - names)]
    problems += ['%s: the contract\'s lifecycle has "%s", which LIFE does not map' % (where, x) for x in sorted(names - set(life_keys))]
    return problems


def check_message_types(handled, schema, where):
    names = message_types(schema)
    problems = ['%s handles message type "%s", which the contract does not declare' % (where, x) for x in sorted(set(handled) - names)]
    problems += ['%s: the contract declares message type "%s", which nothing on the page handles' % (where, x) for x in sorted(names - set(handled))]
    return problems


# ---- a small draft-07-ish validator, so a page test's fixtures (node) can be checked against the
# same schema the page is checked against, without a schema library dependency. Supports: type,
# enum, const, required, properties, additionalProperties, items (schema or tuple), minItems,
# maxItems, minimum, maximum, oneOf, allOf (with if/then/else), $ref -- the subset the MD/MM
# contracts actually use (no patternProperties, anyOf, not, or $ref outside #/$defs/<name>). ----
def _type_ok(inst, t):
    if t == 'null':
        return inst is None
    if t == 'integer':
        return isinstance(inst, int) and not isinstance(inst, bool)
    if t == 'number':
        return isinstance(inst, (int, float)) and not isinstance(inst, bool)
    if t == 'boolean':
        return isinstance(inst, bool)
    if t == 'string':
        return isinstance(inst, str)
    if t == 'object':
        return isinstance(inst, dict)
    if t == 'array':
        return isinstance(inst, list)
    return True


def _resolve(schema, root):
    while '$ref' in schema:
        schema = root['$defs'][schema['$ref'].split('/')[-1]]
    return schema


def _validate(inst, schema, root, path, problems):
    schema = _resolve(schema, root)
    if 'oneOf' in schema:
        if not any(not _sub_problems(inst, sub, root, path) for sub in schema['oneOf']):
            problems.append('%s: matches none of %d alternatives' % (path, len(schema['oneOf'])))
        return
    if 'allOf' in schema:
        for sub in schema['allOf']:
            if 'if' in sub:
                branch = sub.get('then') if not _sub_problems(inst, sub['if'], root, path) else sub.get('else')
                if branch:
                    _validate(inst, branch, root, path, problems)
            else:
                _validate(inst, sub, root, path, problems)
    if 'const' in schema and inst != schema['const']:
        problems.append('%s: expected %r, got %r' % (path, schema['const'], inst))
    if 'enum' in schema and inst not in schema['enum']:
        problems.append('%s: %r not in %r' % (path, inst, schema['enum']))
    t = schema.get('type')
    if t is not None:
        types = t if isinstance(t, list) else [t]
        if not any(_type_ok(inst, x) for x in types):
            problems.append('%s: expected type %s, got %s' % (path, t, type(inst).__name__))
            return
    if isinstance(inst, dict):
        props = schema.get('properties', {})
        problems += ['%s: missing required "%s"' % (path, req) for req in schema.get('required', []) if req not in inst]
        if schema.get('additionalProperties') is False:
            extra = sorted(set(inst) - set(props))
            if extra:
                problems.append('%s: unexpected propert%s %s' % (path, 'y' if len(extra) == 1 else 'ies', ', '.join(extra)))
        for k, v in inst.items():
            if k in props:
                _validate(v, props[k], root, path + '.' + k, problems)
    if isinstance(inst, list):
        items = schema.get('items')
        if isinstance(items, list):
            for i, sub in enumerate(items):
                if i < len(inst):
                    _validate(inst[i], sub, root, '%s[%d]' % (path, i), problems)
        elif items is not None:
            for i, v in enumerate(inst):
                _validate(v, items, root, '%s[%d]' % (path, i), problems)
        if 'minItems' in schema and len(inst) < schema['minItems']:
            problems.append('%s: %d items, fewer than minItems %d' % (path, len(inst), schema['minItems']))
        if 'maxItems' in schema and len(inst) > schema['maxItems']:
            problems.append('%s: %d items, more than maxItems %d' % (path, len(inst), schema['maxItems']))
    if isinstance(inst, (int, float)) and not isinstance(inst, bool):
        if 'minimum' in schema and inst < schema['minimum']:
            problems.append('%s: %r below minimum %r' % (path, inst, schema['minimum']))
        if 'maximum' in schema and inst > schema['maximum']:
            problems.append('%s: %r above maximum %r' % (path, inst, schema['maximum']))


def _sub_problems(inst, schema, root, path):
    p = []
    _validate(inst, schema, root, path, p)
    return p


def validate(instance, def_name, schema, path='$'):
    """instance against schema['$defs'][def_name] -> a list of problems (empty: valid)"""
    problems = []
    _validate(instance, {'$ref': '#/$defs/' + def_name}, schema, path, problems)
    return problems


if __name__ == '__main__':
    schema_path, def_name = sys.argv[1], sys.argv[2]
    result = validate(json.load(sys.stdin), def_name, load(schema_path))
    for p in result:
        print(p)
    sys.exit(1 if result else 0)
