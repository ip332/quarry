"""Render thin public-API adapters from one shared expected-value model.

Each adapter creates its own bytes, checks every decoded field, and re-encodes.
The driver exchanges binary files, rather than inferring interoperability from
separate round trips. C storage exercises wrapper encode/decode, not C aliases.
"""
import json
from model import RECORDS, BY_NAME


def cpp_type(name):
    return name.replace('.', '::')


def stem(name):
    return name.replace('.', '_')


def camel(name):
    return ''.join(p[0].upper() + p[1:] for p in name.split('_'))


def native(storage):
    cpp = storage != 'c'
    owning = storage == 'cpp'
    headers = sorted({name.rsplit('.', 1)[0].replace('.', '/') for name, _, _ in RECORDS})
    ext = '.generated.hpp' if cpp else '.generated.h'
    out = ''.join(f'#include "{h}{ext}"\n' for h in headers)
    out += '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
    if cpp:
        out += '#include <vector>\n#include <span>\n#include <string>\n'
    out += '#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\\n",__LINE__,#x); exit(1); } } while (0)\n'
    for name, _, fields in RECORDS:
        typ = cpp_type(name) if owning else stem(name) + '_t'
        fun = stem(name)
        out += f'static {typ} make_{fun}(int seed) {{\n'
        if owning:
            out += f' {typ}Builder v; if(seed<0) return v.build();\n'
        else:
            out += f' {typ} v; {fun}_init(&v); if(seed<0) return v;\n'
        for field, kind, value in fields:
            if kind.endswith('[]'):
                sub = kind[:-2]
                if owning:
                    out += f' CHECK(v.set_{field}({{make_{stem(sub)}(seed),make_{stem(sub)}(seed+1)}}));\n'
                else:
                    out += f' v.{field}_count=2; v.{field}[0]=make_{stem(sub)}(seed); v.{field}[1]=make_{stem(sub)}(seed+1);\n'
            elif kind == 'string':
                if owning:
                    out += f' CHECK(v.set_{field}("{value}"+std::to_string(seed)));\n'
                else:
                    out += f' v.{field}_length=(uint32_t)snprintf(v.{field},sizeof(v.{field}),"{value}%d",seed);\n'
            else:
                if kind in BY_NAME:
                    expression = f'make_{stem(kind)}(seed)'
                elif kind == 'enum':
                    expression = 'static_cast<matrix::Mode>(3)' if owning else '(matrix_Mode_t)3'
                elif kind == 'bool':
                    expression = 'true' if value else 'false'
                else:
                    expression = f'{value}+seed'
                if owning:
                    out += f' CHECK(v.set_{field}({expression}));\n'
                else:
                    out += f' v.has_{field}=true; v.{field}={expression};\n'
        if name == 'matrix.EnumSample':
            out += ' (void)seed;\n'
        out += ' return v.build();\n}\n' if owning else ' return v;\n}\n'
        out += f'static void check_{fun}(const {typ}* v,int seed) {{\n (void)seed;\n'
        out += ' if(seed<0) {\n'
        for field, kind, _ in fields:
            absent = f'!v->has_{field}()' if owning else (f'v->{field}_count==0' if kind.endswith('[]') else f'v->{field}_length==0' if kind == 'string' else f'!v->has_{field}')
            out += f' CHECK({absent});\n'
        out += ' return; }\n'
        for field, kind, value in fields:
            access = f'v->{field}()' if owning else f'v->{field}'
            if kind.endswith('[]'):
                if owning:
                    out += f' CHECK({access} && {access}->size()==2);\n'
                    for i in range(2):
                        out += f' check_{stem(kind[:-2])}(&(*{access})[{i}],seed+{i});\n'
                else:
                    out += f' CHECK(v->{field}_count==2);\n'
                    for i in range(2):
                        out += f' check_{stem(kind[:-2])}(&v->{field}[{i}],seed+{i});\n'
            elif kind == 'string':
                out += f' char expected[32]; (void)snprintf(expected,sizeof(expected),"{value}%d",seed);\n'
                if owning:
                    out += f' CHECK({access} && *{access}==expected);\n'
                else:
                    out += f' CHECK(v->{field}_length==strlen(expected) && strcmp({access},expected)==0);\n'
            else:
                present = access if owning else f'v->has_{field}'
                out += f' CHECK({present});\n'
                if kind in BY_NAME:
                    ptr = access if owning else '&' + access
                    out += f' check_{stem(kind)}({ptr},seed);\n'
                else:
                    expected = ('static_cast<matrix::Mode>(3)' if owning else '(matrix_Mode_t)3') if kind == 'enum' else ('true' if value else 'false') if kind == 'bool' else f'({"uint" if kind.startswith("u") else "int"}{kind[1:]}_t)({value}+seed)'
                    out += f' CHECK({"*" if owning else ""}{access}=={expected});\n'
        out += '}\n'
    out += r'''
static int request(int argc,char** argv) {
 CHECK(argc==5); /* encode/decode, type, input/output file, seed */
 int seed=atoi(argv[4]); unsigned char wire[65536]; size_t size=0;
 if(strcmp(argv[1],"identity")==0) {
'''
    identity = 'truck::kQuarrySchemaId.data()' if owning else 'quarry_ns_truck_schema_id()'
    out += f' const uint8_t* id={identity}; for(size_t i=0;i<16;i++) printf("%02x",id[i]); return 0;\n }}\n'
    out += ' if(strcmp(argv[1],"decode")==0) { FILE* f=fopen(argv[3],"rb"); CHECK(f); size=fread(wire,1,sizeof(wire),f); CHECK(!ferror(f)); fclose(f); }\n'
    for index, (name, _, _) in enumerate(RECORDS):
        fun = stem(name)
        typ = cpp_type(name)
        out += f' {"if" if index == 0 else "else if"}(strcmp(argv[2],"{name}")==0) {{\n'
        if owning:
            out += f' if(strcmp(argv[1],"encode")==0) {{ auto v=make_{fun}(seed); auto e={typ.rsplit("::",1)[0]}::encode_result(v); CHECK(e); size=e.value->size(); CHECK(size<=sizeof(wire)); memcpy(wire,e.value->data(),size); }}\n'
            out += f' else {{ auto d={typ.rsplit("::",1)[0]}::decode_{name.split(".")[-1]}_result(std::span<const std::byte>(reinterpret_cast<const std::byte*>(wire),size)); CHECK(d); check_{fun}(&*d.value,seed); auto e={typ.rsplit("::",1)[0]}::encode_result(*d.value); CHECK(e && e.value->size()==size && memcmp(e.value->data(),wire,size)==0); }}\n'
        elif storage == 'cpp-c':
            out += f' quarry::cxx::{fun} v; if(strcmp(argv[1],"encode")==0) {{ v.native()=make_{fun}(seed); auto e=v.encode(std::span<std::byte>(reinterpret_cast<std::byte*>(wire),sizeof(wire))); CHECK(e.status==QUARRY_C_STATUS_OK); size=e.bytes_written; }}\n'
            out += f' else {{ auto d=v.decode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(wire),size)); CHECK(d.status==QUARRY_C_STATUS_OK); check_{fun}(&v.native(),seed); unsigned char again[65536]; auto e=v.encode(std::span<std::byte>(reinterpret_cast<std::byte*>(again),sizeof(again))); CHECK(e.status==QUARRY_C_STATUS_OK && e.bytes_written==size && memcmp(again,wire,size)==0); }}\n'
        else:
            out += f' if(strcmp(argv[1],"encode")==0) {{ {fun}_t v=make_{fun}(seed); {fun}_encode_result_t e={fun}_encode(&v,wire,sizeof(wire)); CHECK(e.status==QUARRY_C_STATUS_OK); size=e.bytes_written; }}\n'
            out += f' else {{ {fun}_decode_result_t d={fun}_decode(wire,size); CHECK(d.status==QUARRY_C_STATUS_OK); check_{fun}(&d.value,seed); unsigned char again[65536]; {fun}_encode_result_t e={fun}_encode(&d.value,again,sizeof(again)); CHECK(e.status==QUARRY_C_STATUS_OK && e.bytes_written==size && memcmp(again,wire,size)==0); }}\n'
        out += ' }\n'
    out += ' else { CHECK(0); }\n if(strcmp(argv[1],"encode")==0) { FILE* f=fopen(argv[3],"wb"); CHECK(f); CHECK(fwrite(wire,1,size,f)==size); CHECK(fclose(f)==0); } return 0;\n}\n'
    out += r'''
int main(int argc,char** argv) {
 if(argc!=3 || strcmp(argv[1],"--batch")!=0) return request(argc,argv);
 FILE* input=fopen(argv[2],"r"); CHECK(input);
 char lines[4][4096]; char* args[5]={argv[0],lines[0],lines[1],lines[2],lines[3]};
 while(fgets(lines[0],sizeof(lines[0]),input)) {
  for(unsigned i=1;i<4;i++) CHECK(fgets(lines[i],sizeof(lines[i]),input));
  for(unsigned i=0;i<4;i++) lines[i][strcspn(lines[i],"\n")]=0;
  CHECK(request(5,args)==0);
 }
 CHECK(!ferror(input)); CHECK(fclose(input)==0); return 0;
}
'''
    return out


def python():
    out = 'import sys\nfrom pathlib import Path\n'
    for name, _, _ in RECORDS:
        ns, typ = name.rsplit('.', 1)
        out += f'from {ns}.schema import {typ}\n'
    out += 'from truck.schema import QUARRY_SCHEMA_ID\n'
    out += 'def make(name, seed):\n'
    for name, _, _ in RECORDS:
        out += f' if seed<0 and name=="{name}": return {name.split(".")[-1]}()\n'
    for name, _, fields in RECORDS:
        args = []
        for field, kind, value in fields:
            if kind.endswith('[]'):
                expr = f'[make("{kind[:-2]}",seed),make("{kind[:-2]}",seed+1)]'
            elif kind in BY_NAME:
                expr = f'make("{kind}",seed)'
            elif kind == 'string':
                expr = repr(value) + '+str(seed)'
            elif kind in ('bool', 'enum'):
                expr = repr(value)
            else:
                expr = str(value) + '+seed'
            args.append(field + '=' + expr)
        out += f' if name=="{name}": return {name.split(".")[-1]}({",".join(args)})\n'
    out += '''def request(args):
 mode,name,path,seed=args
 seed=int(seed)
 if mode=='identity': print(QUARRY_SCHEMA_ID.hex()); return
 expected=make(name,seed)
 if mode=='encode': Path(path).write_bytes(expected.encode())
 else:
  data=Path(path).read_bytes()
  decoded=type(expected).decode(data)
  assert decoded==expected, (decoded,expected)
  assert decoded.encode()==data
if sys.argv[1]=='--batch':
 lines=Path(sys.argv[2]).read_text().splitlines()
 assert len(lines)%4==0
 for i in range(0,len(lines),4): request(lines[i:i+4])
else: request(sys.argv[1:])
'''
    return out


def go():
    namespaces = sorted({n.rsplit('.', 1)[0] for n, _, _ in RECORDS})
    aliases = {n: f'ns{i}' for i, n in enumerate(namespaces)}
    def typ(name):
        ns, short = name.rsplit('.', 1)
        return aliases[ns] + '.' + short
    out = 'package main\nimport("bytes";"fmt";"os";"strconv";"reflect";"strings"; "github.com/ip332/quarry/runtime/go/brf"\n'
    for ns in namespaces:
        out += f'{aliases[ns]} "example.com/quarry/matrix/{ns.replace(".","/")}"\n'
    out += ')\n'
    types = {'u64': 'uint64', 'u32': 'uint32', 'u16': 'uint16', 'u8': 'uint8', 'i16': 'int16', 'i8': 'int8'}
    for name, _, fields in RECORDS:
        out += f'func make{stem(name)}(seed int) *{typ(name)} {{ if seed<0 {{ return &{typ(name)}{{}} }}; return &{typ(name)}{{'
        for field, kind, value in fields:
            if kind.endswith('[]'):
                expr = f'[]{typ(kind[:-2])}{{*make{stem(kind[:-2])}(seed),*make{stem(kind[:-2])}(seed+1)}}'
            elif kind in BY_NAME:
                expr = f'make{stem(kind)}(seed)'
            elif kind == 'string':
                expr = json.dumps(value) + '+strconv.Itoa(seed)'
            elif kind == 'enum':
                expr = f'brf.Ptr({aliases["matrix"]}.Mode(3))'
            elif kind == 'bool':
                expr = 'brf.Ptr(true)' if value else 'brf.Ptr(false)'
            else:
                expr = f'brf.Ptr({types[kind]}({value}+seed))'
            out += f'{camel(field)}:{expr},'
        out += '} }\n'
    out += '''type codec interface { MarshalBRF() ([]byte,error); UnmarshalBRF([]byte) error }
func request(args []string) { mode,name,path:=args[0],args[1],args[2]; seed,err:=strconv.Atoi(args[3]);if err!=nil{panic(err)}
'''
    out += f'if mode=="identity"{{fmt.Printf("%x",{aliases["truck"]}.QuarrySchemaID());return}}\n'
    out += 'var expected,decoded codec\nswitch name {\n'
    for name, _, _ in RECORDS:
        out += f'case "{name}":expected=make{stem(name)}(seed);decoded=&{typ(name)}{{}}\n'
    out += '''default:panic(name) }
if mode=="encode" { data,e:=expected.MarshalBRF();if e!=nil{panic(e)};if e=os.WriteFile(path,data,0600);e!=nil{panic(e)};return }
data,e:=os.ReadFile(path);if e!=nil{panic(e)};if e=decoded.UnmarshalBRF(data);e!=nil{panic(e)}
if !reflect.DeepEqual(expected,decoded){panic(fmt.Sprintf("values: want %+v got %+v",expected,decoded))}
again,e:=decoded.MarshalBRF();if e!=nil||!bytes.Equal(data,again){panic("re-encode mismatch")}
}
'''
    out += '''func main(){
 if os.Args[1]!="--batch" {request(os.Args[1:]);return}
 data,err:=os.ReadFile(os.Args[2]);if err!=nil{panic(err)}
 lines:=strings.Split(strings.TrimSuffix(string(data),"\\n"),"\\n")
 if len(lines)%4!=0{panic("batch protocol")}
 for i:=0;i<len(lines);i+=4{request(lines[i:i+4])}
}
'''
    return out


def generic_c():
    out = r'''
#include "quarry/runtime_c/generic_brf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"generic C line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static quarry_qbs_record_view_t records[32];
static quarry_qbs_field_view_t fields[256];
static quarry_qbs_type_view_t types[128];
static quarry_qbs_enum_view_t enums[32];
static uint64_t enum_values[256];
static quarry_brf_record_node_t nodes[256];
static quarry_brf_field_state_t states[2048];
static uint32_t maps[2048], elements[256];
static quarry_brf_child_relation_t children[256];
static quarry_brf_record_array_relation_t arrays[128];
static quarry_brf_validation_frame_t frames[64];
static quarry_workspace_t workspace;
static const char* prefix;
static unsigned serial;
static void save(const char* name,const quarry_brf_record_view_t* view,int seed) {
 if(strcmp(prefix,"-")==0) return;
 char path[4096]; CHECK(snprintf(path,sizeof(path),"%s-%u.brf",prefix,serial++)>0);
 FILE* f=fopen(path,"wb"); CHECK(f); CHECK(fwrite(view->bytes,1,view->size,f)==view->size); CHECK(fclose(f)==0);
 printf("%s %d %s\n",name,seed,path);
}
'''
    for name, identity, fields in RECORDS:
        out += f'static void check_{stem(name)}(const quarry_brf_record_view_t* v,int seed) {{\n CHECK(v->schema->record_id=={identity}U); (void)seed;\n'
        out += ' if(seed<0) {\n'
        for index in range(len(fields)):
            out += f' bool present{index}; CHECK(quarry_brf_field_is_present(v,{index},&present{index})==QUARRY_GENERIC_OK && !present{index});\n'
        out += ' return; }\n'
        for index, (_, kind, value) in enumerate(fields):
            out += ' {\n'
            if kind in BY_NAME:
                out += f' quarry_brf_record_view_t child; CHECK(quarry_brf_get_record(v,{index},&child)==QUARRY_GENERIC_OK); save("{kind}",&child,seed); check_{stem(kind)}(&child,seed);\n'
            elif kind.endswith('[]'):
                out += f' quarry_brf_array_view_t a; CHECK(quarry_brf_get_record_array(v,{index},&a)==QUARRY_GENERIC_OK && a.count==2);\n'
                out += f' for(unsigned i=0;i<2;i++) {{ quarry_brf_record_view_t child; CHECK(quarry_brf_record_array_get(v,&a,i,&child)==QUARRY_GENERIC_OK); save("{kind[:-2]}",&child,seed+(int)i); check_{stem(kind[:-2])}(&child,seed+(int)i); }}\n'
            elif kind == 'string':
                out += f' quarry_string_view_t s; char expected[32]; (void)snprintf(expected,sizeof(expected),"{value}%d",seed); CHECK(quarry_brf_get_string(v,{index},&s)==QUARRY_GENERIC_OK && s.size==strlen(expected) && memcmp(s.data,expected,s.size)==0);\n'
            elif kind == 'bool':
                out += f' bool b; CHECK(quarry_brf_get_bool(v,{index},&b)==QUARRY_GENERIC_OK && b=={str(value).lower()});\n'
            else:
                signed = kind.startswith('i') or kind == 'enum'
                out += f' {"int64_t" if signed else "uint64_t"} n; CHECK(quarry_brf_get_{"enum" if kind == "enum" else "int" if signed else "uint"}(v,{index},&n)==QUARRY_GENERIC_OK && n==({"int64_t" if signed else "uint64_t"})({value}+{"0" if kind == "enum" else "seed"}));\n'
            out += ' }\n'
        out += '}\n'
    out += r'''
static size_t load(const char* path,uint8_t* data,size_t capacity) {
 FILE* f=fopen(path,"rb"); CHECK(f); size_t size=fread(data,1,capacity,f); CHECK(!ferror(f) && feof(f)); CHECK(fclose(f)==0); return size;
}
int main(int argc,char** argv) {
 CHECK(argc==7); int seed=atoi(argv[5]); prefix=argv[6];
 static uint8_t qbs_bytes[65536],brf_bytes[65536];
 size_t qbs_size=load(argv[3],qbs_bytes,sizeof(qbs_bytes));
 size_t brf_size=load(argv[4],brf_bytes,sizeof(brf_bytes));
 workspace.records=records; workspace.record_capacity=32;
 workspace.fields=fields; workspace.field_capacity=256;
 workspace.types=types; workspace.type_capacity=128;
 workspace.enums=enums; workspace.enum_capacity=32;
 workspace.enum_values=enum_values; workspace.enum_value_capacity=256;
 workspace.nodes=nodes; workspace.node_capacity=256;
 workspace.field_states=states; workspace.field_state_capacity=2048;
 workspace.field_maps=maps; workspace.field_map_capacity=2048;
 workspace.children=children; workspace.child_capacity=256;
 workspace.arrays=arrays; workspace.array_capacity=128;
 workspace.array_elements=elements; workspace.array_element_capacity=256;
 workspace.frames=frames; workspace.frame_capacity=64;
 quarry_qbs_view_t qbs; CHECK(quarry_qbs_parse(qbs_bytes,qbs_size,&qbs,&workspace,NULL)==QUARRY_GENERIC_OK);
 quarry_schema_id_t id; CHECK(quarry_qbs_schema_id(&qbs,&id)==QUARRY_GENERIC_OK);
 const quarry_qbs_record_view_t* record;
 CHECK(quarry_qbs_find_record_by_id(&qbs,(uint32_t)strtoul(argv[2],NULL,10),&record)==QUARRY_GENERIC_OK);
 quarry_brf_record_view_t view;
 CHECK(quarry_brf_validate_with_workspace(&qbs,record,brf_bytes,brf_size,&view,&workspace,NULL)==QUARRY_GENERIC_OK);
'''
    for i, (name, _, _) in enumerate(RECORDS):
        out += f' {"if" if i == 0 else "else if"}(strcmp(argv[1],"{name}")==0) check_{stem(name)}(&view,seed);\n'
    return out + ' else { CHECK(0); } return 0;\n}\n'


def prefixed(variant):
    """Use the same adapter against rev_a APIs, retaining canonical fixture names."""
    if variant == 'python':
        return python().replace('from truck.', 'from rev_a.truck.')
    if variant == 'go':
        return go().replace('example.com/quarry/matrix/truck',
                            'example.com/quarry/matrix/rev_a/truck')
    text = native(variant).replace('#include "truck', '#include "rev_a/truck')
    if variant == 'cpp':
        return text.replace('truck::', 'rev_a::truck::')
    return text.replace('truck_', 'revq0a_truck_')
