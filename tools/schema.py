from pathlib import Path
import copy, hashlib, re

TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]|[^\s{}"]+', re.S)

def keyvalues(text):
    tokens = [t for t in TOKEN.findall(text) if not t.startswith(('//', '/*'))]
    index = 0
    def word(token):
        if not token.startswith('"'): return token
        return re.sub(r'\\(["\\nrt])', lambda m: {'n':'\n','r':'\r','t':'\t'}.get(m[1],m[1]), token[1:-1])
    def block(nested=False):
        nonlocal index
        result = {}
        while index < len(tokens):
            token = tokens[index]; index += 1
            if token == '}':
                if not nested: raise ValueError('Unexpected closing brace')
                return result
            if token.startswith('['): continue
            key = word(token)
            if index >= len(tokens): raise ValueError('Missing value for '+key)
            value = tokens[index]; index += 1
            result[key] = block(True) if value == '{' else word(value)
        if nested: raise ValueError('Unclosed KeyValues block')
        return result
    return block()

def merge(base, override):
    out = copy.deepcopy(base)
    for k,v in override.items():
        out[k] = merge(out[k],v) if isinstance(out.get(k),dict) and isinstance(v,dict) else copy.deepcopy(v)
    return out

def build(schema):
    root = keyvalues(schema.read_text(encoding='utf-8-sig'))['items_game']
    prefabs, attributes = root['prefabs'], root['attributes']
    cache = {}
    def resolve(data, chain=()):
        out = {}
        for name in data.get('prefab','').split():
            if name in chain: raise ValueError('Prefab cycle: '+name)
            if name not in prefabs: raise ValueError('Unknown prefab: '+name)
            if name not in cache: cache[name] = resolve(prefabs[name],chain+(name,))
            out = merge(out,cache[name])
        return merge(out,data)
    by_name = {v.get('name'): dict(v,id=int(k)) for k,v in attributes.items() if k.isdigit()}
    items, weapons = {}, {}
    for definition,data in root['items'].items():
        if not definition.isdigit() or not isinstance(data,dict): continue
        resolved = resolve(data)
        row = {'name':resolved.get('name',''), 'class':resolved.get('item_class',''),
               'slot':resolved.get('item_slot',''), 'type':resolved.get('item_type_name',''),
               'prefabs':data.get('prefab','').split(), 'classes':resolved.get('used_by_classes',{}),
               'attributes':{}, 'styles':resolved.get('visuals',{}).get('styles',{})}
        for name,v in resolved.get('attributes',{}).items():
            attr = by_name.get(name,{})
            row['attributes'][name] = {'id':attr.get('id'), 'class':v.get('attribute_class',attr.get('attribute_class','')),
                'value':v.get('value') if isinstance(v,dict) else v, 'integer':attr.get('stored_as_integer','0')=='1'}
        for name,v in resolved.get('static_attrs',{}).items():
            attr = by_name.get(name,{})
            row['attributes'][name] = {'id':attr.get('id'), 'class':attr.get('attribute_class',''), 'value':v,
                                      'integer':attr.get('stored_as_integer','0')=='1'}
        items[definition] = row
        if row['class'].startswith('tf_weapon_'): weapons[definition] = row
    result = {'source':str(schema), 'source_sha256':hashlib.sha256(schema.read_bytes()).hexdigest(),
              'counts':{'items':len(items),'weapons':len(weapons),'attributes':len(attributes),'prefabs':len(prefabs)},
              'attributes':attributes, 'items':items}
    return result,weapons

