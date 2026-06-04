import struct
import sys

def parse_uleb128(data, off):
    result = 0
    shift = 0
    while True:
        b = data[off]
        off += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return result, off

import zipfile

def read_dex_method(dex_path, class_desc, method_name):
    if dex_path.endswith('.apk'):
        with zipfile.ZipFile(dex_path, 'r') as z:
            data = z.read('classes.dex')
    else:
        with open(dex_path, 'rb') as f:
            data = f.read()
    
    # Read header
    magic = data[0:8]
    string_ids_size, string_ids_off = struct.unpack('<II', data[56:64])
    type_ids_size, type_ids_off = struct.unpack('<II', data[64:72])
    proto_ids_size, proto_ids_off = struct.unpack('<II', data[72:80])
    field_ids_size, field_ids_off = struct.unpack('<II', data[80:88])
    method_ids_size, method_ids_off = struct.unpack('<II', data[88:96])
    class_defs_size, class_defs_off = struct.unpack('<II', data[96:104])
    
    # Helper to get string
    def get_string(idx):
        if idx >= string_ids_size: return None
        off = struct.unpack('<I', data[string_ids_off + idx * 4:string_ids_off + idx * 4 + 4])[0]
        length, off = parse_uleb128(data, off)
        # Read until null or length
        return data[off:off+length].decode('utf-8', errors='ignore')
    
    # Helper to get type desc
    def get_type(idx):
        if idx >= type_ids_size: return None
        str_idx = struct.unpack('<I', data[type_ids_off + idx * 4:type_ids_off + idx * 4 + 4])[0]
        return get_string(str_idx)
        
    # Helper to get method signature
    def get_method_name_and_class(idx):
        class_idx, proto_idx, name_idx = struct.unpack('<HHI', data[method_ids_off + idx * 8:method_ids_off + idx * 8 + 8])
        return get_type(class_idx), get_string(name_idx)

    # Find class def
    class_def_off = None
    for i in range(class_defs_size):
        off = class_defs_off + i * 32
        class_idx = struct.unpack('<I', data[off:off+4])[0]
        desc = get_type(class_idx)
        if desc == class_desc:
            class_def_off = off
            break
            
    if not class_def_off:
        return f"Class {class_desc} not found"
        
    class_data_off = struct.unpack('<I', data[class_def_off+24:class_def_off+28])[0]
    if class_data_off == 0:
        return "No class data"
        
    # Parse class data
    off = class_data_off
    static_fields_size, off = parse_uleb128(data, off)
    instance_fields_size, off = parse_uleb128(data, off)
    direct_methods_size, off = parse_uleb128(data, off)
    virtual_methods_size, off = parse_uleb128(data, off)
    
    # Skip fields
    for _ in range(static_fields_size):
        _, off = parse_uleb128(data, off) # field_idx_diff
        _, off = parse_uleb128(data, off) # access_flags
    for _ in range(instance_fields_size):
        _, off = parse_uleb128(data, off)
        _, off = parse_uleb128(data, off)
        
    # Find direct method
    method_idx = 0
    for _ in range(direct_methods_size):
        method_idx_diff, off = parse_uleb128(data, off)
        method_idx += method_idx_diff
        access_flags, off = parse_uleb128(data, off)
        code_off, off = parse_uleb128(data, off)
        c_desc, m_name = get_method_name_and_class(method_idx)
        if m_name == method_name:
            return code_off, data
            
    # Find virtual method
    method_idx = 0
    for _ in range(virtual_methods_size):
        method_idx_diff, off = parse_uleb128(data, off)
        method_idx += method_idx_diff
        access_flags, off = parse_uleb128(data, off)
        code_off, off = parse_uleb128(data, off)
        c_desc, m_name = get_method_name_and_class(method_idx)
        if m_name == method_name:
            return code_off, data
            
    return f"Method {method_name} not found"

def compare(desc, mname):
    orig_res = read_dex_method('apk/Current Activity_1.5.5_APKPure.apk', desc, mname)
    test_res = read_dex_method('apk/work/classes_test.dex', desc, mname)
    
    if isinstance(orig_res, str):
        print("Original error:", orig_res)
        return
    if isinstance(test_res, str):
        print("Test error:", test_res)
        return
        
    orig_off, orig_data = orig_res
    test_off, test_data = test_res
    
    print(f"Comparing {desc}->{mname}")
    print(f"Original Code Offset: 0x{orig_off:x}")
    print(f"Test Code Offset: 0x{test_off:x}")
    
    # Read Code Item headers
    # registers_size, ins_size, outs_size, tries_size, debug_info_off, insns_size
    o_regs, o_ins, o_outs, o_tries, o_dbg, o_insns_sz = struct.unpack('<HHHHII', orig_data[orig_off:orig_off+16])
    t_regs, t_ins, t_outs, t_tries, t_dbg, t_insns_sz = struct.unpack('<HHHHII', test_data[test_off:test_off+16])
    
    print(f"Original Code Item: regs={o_regs}, ins={o_ins}, outs={o_outs}, tries={o_tries}, dbg=0x{o_dbg:x}, insns_sz={o_insns_sz}")
    print(f"Test Code Item:     regs={t_regs}, ins={t_ins}, outs={t_outs}, tries={t_tries}, dbg=0x{t_dbg:x}, insns_sz={t_insns_sz}")
    
    # Read instructions
    o_insns = orig_data[orig_off+16:orig_off+16+o_insns_sz*2]
    t_insns = test_data[test_off+16:test_off+16+t_insns_sz*2]
    
    print("Instruction bytes:")
    print("Original (words):", [f"0x{w:04x}" for w in struct.unpack(f'<{o_insns_sz}H', o_insns)])
    print("Test (words):    ", [f"0x{w:04x}" for w in struct.unpack(f'<{t_insns_sz}H', t_insns)])

if __name__ == '__main__':
    compare('Landroid/support/a/a/as;', 'a')
