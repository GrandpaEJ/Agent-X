import re

with open('src/android/smali/smali_writer.c', 'r') as f:
    lines = f.readlines()

start_idx = -1
end_idx = -1

for i, line in enumerate(lines):
    if "uint32_t *annot_offsets = calloc(class_count, 4);" in line:
        start_idx = i
    if "for (uint32_t i = 0; i < class_count; i++) {" in line and "smali_class_def_t *c = &ctx->classes[i];" in lines[i+1] and "uint32_t def_off = class_defs_off + i * 32;" in lines[i+2]:
        end_idx = i

if start_idx == -1 or end_idx == -1:
    print("Markers not found!")
    print(start_idx, end_idx)
    exit(1)

new_logic = """    uint32_t *annot_offsets = calloc(class_count, 4);
    uint32_t first_annot_dir_off = 0, annot_dir_count = 0;
    uint32_t first_annot_set_off = 0, annot_set_count = 0;
    uint32_t first_annot_item_off = 0, annot_item_count = 0;
    uint32_t first_annot_set_ref_list_off = 0, annot_set_ref_list_count = 0;

    uint32_t **class_annot_item_offs = calloc(class_count, sizeof(uint32_t*));
    uint32_t *class_set_offs = calloc(class_count, sizeof(uint32_t));

    uint32_t ***meth_d_annot_item_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **meth_d_set_offs = calloc(class_count, sizeof(uint32_t*));
    
    uint32_t ***meth_v_annot_item_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **meth_v_set_offs = calloc(class_count, sizeof(uint32_t*));

    uint32_t ***field_s_annot_item_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **field_s_set_offs = calloc(class_count, sizeof(uint32_t*));

    uint32_t ***field_i_annot_item_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **field_i_set_offs = calloc(class_count, sizeof(uint32_t*));

    uint32_t ****param_d_annot_item_offs = calloc(class_count, sizeof(uint32_t***));
    uint32_t ***param_d_set_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **param_d_ref_list_offs = calloc(class_count, sizeof(uint32_t*));

    uint32_t ****param_v_annot_item_offs = calloc(class_count, sizeof(uint32_t***));
    uint32_t ***param_v_set_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **param_v_ref_list_offs = calloc(class_count, sizeof(uint32_t*));

    // Pass 1: Write all annotation_item
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        
        if (c->annot_count > 0 && c->annots[0].type) {
            uint32_t ic = c->annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->annot_count;
            class_annot_item_offs[i] = calloc(ic, sizeof(uint32_t));
            for (uint32_t ai = 0; ai < ic; ai++) {
                class_annot_item_offs[i][ai] = b.len;
                annot_item_count++;
                if (first_annot_item_off == 0) first_annot_item_off = b.len;
                buf_write_u8(&b, c->annots[ai].visibility);
                write_encoded_annotation(&b, ctx, &c->annots[ai]);
            }
        }
        
        field_s_annot_item_offs[i] = calloc(c->static_field_count, sizeof(uint32_t*));
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            if (c->static_fields[j].annot_count > 0) {
                uint32_t ac = c->static_fields[j].annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->static_fields[j].annot_count;
                field_s_annot_item_offs[i][j] = calloc(ac, sizeof(uint32_t));
                for (uint32_t ai = 0; ai < ac; ai++) {
                    field_s_annot_item_offs[i][j][ai] = b.len;
                    annot_item_count++;
                    if (first_annot_item_off == 0) first_annot_item_off = b.len;
                    buf_write_u8(&b, c->static_fields[j].annots[ai].visibility);
                    write_encoded_annotation(&b, ctx, &c->static_fields[j].annots[ai]);
                }
            }
        }

        field_i_annot_item_offs[i] = calloc(c->instance_field_count, sizeof(uint32_t*));
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            if (c->instance_fields[j].annot_count > 0) {
                uint32_t ac = c->instance_fields[j].annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->instance_fields[j].annot_count;
                field_i_annot_item_offs[i][j] = calloc(ac, sizeof(uint32_t));
                for (uint32_t ai = 0; ai < ac; ai++) {
                    field_i_annot_item_offs[i][j][ai] = b.len;
                    annot_item_count++;
                    if (first_annot_item_off == 0) first_annot_item_off = b.len;
                    buf_write_u8(&b, c->instance_fields[j].annots[ai].visibility);
                    write_encoded_annotation(&b, ctx, &c->instance_fields[j].annots[ai]);
                }
            }
        }

        meth_d_annot_item_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t*));
        param_d_annot_item_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t**));
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            smali_method_def_t *m = &c->direct_methods[j];
            if (m->annot_count > 0) {
                uint32_t ac = m->annot_count > MAX_ANNOTS ? MAX_ANNOTS : m->annot_count;
                meth_d_annot_item_offs[i][j] = calloc(ac, sizeof(uint32_t));
                for (uint32_t ai = 0; ai < ac; ai++) {
                    meth_d_annot_item_offs[i][j][ai] = b.len;
                    annot_item_count++;
                    if (first_annot_item_off == 0) first_annot_item_off = b.len;
                    buf_write_u8(&b, m->annots[ai].visibility);
                    write_encoded_annotation(&b, ctx, &m->annots[ai]);
                }
            }
            if (m->param_name_count > 0 && m->param_annots) {
                param_d_annot_item_offs[i][j] = calloc(m->param_name_count, sizeof(uint32_t*));
                for (uint32_t pidx = 0; pidx < m->param_name_count; pidx++) {
                    if (m->param_annot_counts && m->param_annot_counts[pidx] > 0) {
                        uint32_t ac = m->param_annot_counts[pidx] > MAX_ANNOTS ? MAX_ANNOTS : m->param_annot_counts[pidx];
                        param_d_annot_item_offs[i][j][pidx] = calloc(ac, sizeof(uint32_t));
                        for (uint32_t ai = 0; ai < ac; ai++) {
                            param_d_annot_item_offs[i][j][pidx][ai] = b.len;
                            annot_item_count++;
                            if (first_annot_item_off == 0) first_annot_item_off = b.len;
                            buf_write_u8(&b, m->param_annots[pidx][ai].visibility);
                            write_encoded_annotation(&b, ctx, &m->param_annots[pidx][ai]);
                        }
                    }
                }
            }
        }

        meth_v_annot_item_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t*));
        param_v_annot_item_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t**));
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            smali_method_def_t *m = &c->virtual_methods[j];
            if (m->annot_count > 0) {
                uint32_t ac = m->annot_count > MAX_ANNOTS ? MAX_ANNOTS : m->annot_count;
                meth_v_annot_item_offs[i][j] = calloc(ac, sizeof(uint32_t));
                for (uint32_t ai = 0; ai < ac; ai++) {
                    meth_v_annot_item_offs[i][j][ai] = b.len;
                    annot_item_count++;
                    if (first_annot_item_off == 0) first_annot_item_off = b.len;
                    buf_write_u8(&b, m->annots[ai].visibility);
                    write_encoded_annotation(&b, ctx, &m->annots[ai]);
                }
            }
            if (m->param_name_count > 0 && m->param_annots) {
                param_v_annot_item_offs[i][j] = calloc(m->param_name_count, sizeof(uint32_t*));
                for (uint32_t pidx = 0; pidx < m->param_name_count; pidx++) {
                    if (m->param_annot_counts && m->param_annot_counts[pidx] > 0) {
                        uint32_t ac = m->param_annot_counts[pidx] > MAX_ANNOTS ? MAX_ANNOTS : m->param_annot_counts[pidx];
                        param_v_annot_item_offs[i][j][pidx] = calloc(ac, sizeof(uint32_t));
                        for (uint32_t ai = 0; ai < ac; ai++) {
                            param_v_annot_item_offs[i][j][pidx][ai] = b.len;
                            annot_item_count++;
                            if (first_annot_item_off == 0) first_annot_item_off = b.len;
                            buf_write_u8(&b, m->param_annots[pidx][ai].visibility);
                            write_encoded_annotation(&b, ctx, &m->param_annots[pidx][ai]);
                        }
                    }
                }
            }
        }
    }
    
    // Pass 2: Write all annotation_set_item
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        
        if (class_annot_item_offs[i]) {
            align_4(&b);
            class_set_offs[i] = b.len;
            annot_set_count++;
            if (first_annot_set_off == 0) first_annot_set_off = b.len;
            uint32_t ic = c->annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->annot_count;
            buf_write_u32(&b, ic);
            for (uint32_t ai = 0; ai < ic; ai++) buf_write_u32(&b, class_annot_item_offs[i][ai]);
        }
        
        field_s_set_offs[i] = calloc(c->static_field_count, sizeof(uint32_t));
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            if (field_s_annot_item_offs[i][j]) {
                align_4(&b);
                field_s_set_offs[i][j] = b.len;
                annot_set_count++;
                if (first_annot_set_off == 0) first_annot_set_off = b.len;
                uint32_t ac = c->static_fields[j].annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->static_fields[j].annot_count;
                buf_write_u32(&b, ac);
                for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, field_s_annot_item_offs[i][j][ai]);
            }
        }

        field_i_set_offs[i] = calloc(c->instance_field_count, sizeof(uint32_t));
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            if (field_i_annot_item_offs[i][j]) {
                align_4(&b);
                field_i_set_offs[i][j] = b.len;
                annot_set_count++;
                if (first_annot_set_off == 0) first_annot_set_off = b.len;
                uint32_t ac = c->instance_fields[j].annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->instance_fields[j].annot_count;
                buf_write_u32(&b, ac);
                for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, field_i_annot_item_offs[i][j][ai]);
            }
        }

        meth_d_set_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t));
        param_d_set_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t*));
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            smali_method_def_t *m = &c->direct_methods[j];
            if (meth_d_annot_item_offs[i][j]) {
                align_4(&b);
                meth_d_set_offs[i][j] = b.len;
                annot_set_count++;
                if (first_annot_set_off == 0) first_annot_set_off = b.len;
                uint32_t ac = m->annot_count > MAX_ANNOTS ? MAX_ANNOTS : m->annot_count;
                buf_write_u32(&b, ac);
                for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, meth_d_annot_item_offs[i][j][ai]);
            }
            if (param_d_annot_item_offs[i][j]) {
                param_d_set_offs[i][j] = calloc(m->param_name_count, sizeof(uint32_t));
                for (uint32_t pidx = 0; pidx < m->param_name_count; pidx++) {
                    if (param_d_annot_item_offs[i][j][pidx]) {
                        align_4(&b);
                        param_d_set_offs[i][j][pidx] = b.len;
                        annot_set_count++;
                        if (first_annot_set_off == 0) first_annot_set_off = b.len;
                        uint32_t ac = m->param_annot_counts[pidx] > MAX_ANNOTS ? MAX_ANNOTS : m->param_annot_counts[pidx];
                        buf_write_u32(&b, ac);
                        for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, param_d_annot_item_offs[i][j][pidx][ai]);
                    }
                }
            }
        }

        meth_v_set_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t));
        param_v_set_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t*));
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            smali_method_def_t *m = &c->virtual_methods[j];
            if (meth_v_annot_item_offs[i][j]) {
                align_4(&b);
                meth_v_set_offs[i][j] = b.len;
                annot_set_count++;
                if (first_annot_set_off == 0) first_annot_set_off = b.len;
                uint32_t ac = m->annot_count > MAX_ANNOTS ? MAX_ANNOTS : m->annot_count;
                buf_write_u32(&b, ac);
                for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, meth_v_annot_item_offs[i][j][ai]);
            }
            if (param_v_annot_item_offs[i][j]) {
                param_v_set_offs[i][j] = calloc(m->param_name_count, sizeof(uint32_t));
                for (uint32_t pidx = 0; pidx < m->param_name_count; pidx++) {
                    if (param_v_annot_item_offs[i][j][pidx]) {
                        align_4(&b);
                        param_v_set_offs[i][j][pidx] = b.len;
                        annot_set_count++;
                        if (first_annot_set_off == 0) first_annot_set_off = b.len;
                        uint32_t ac = m->param_annot_counts[pidx] > MAX_ANNOTS ? MAX_ANNOTS : m->param_annot_counts[pidx];
                        buf_write_u32(&b, ac);
                        for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, param_v_annot_item_offs[i][j][pidx][ai]);
                    }
                }
            }
        }
    }

    // Pass 2.5: Write annotation_set_ref_list for parameter annotations
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        param_d_ref_list_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t));
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            if (param_d_annot_item_offs[i][j]) {
                int has_param_annot = 0;
                for (uint32_t pidx = 0; pidx < c->direct_methods[j].param_name_count; pidx++) {
                    if (param_d_set_offs[i][j][pidx]) has_param_annot = 1;
                }
                if (has_param_annot) {
                    align_4(&b);
                    param_d_ref_list_offs[i][j] = b.len;
                    annot_set_ref_list_count++;
                    if (first_annot_set_ref_list_off == 0) first_annot_set_ref_list_off = b.len;
                    buf_write_u32(&b, c->direct_methods[j].param_name_count);
                    for (uint32_t pidx = 0; pidx < c->direct_methods[j].param_name_count; pidx++) {
                        buf_write_u32(&b, param_d_set_offs[i][j][pidx]);
                    }
                }
            }
        }
        param_v_ref_list_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t));
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            if (param_v_annot_item_offs[i][j]) {
                int has_param_annot = 0;
                for (uint32_t pidx = 0; pidx < c->virtual_methods[j].param_name_count; pidx++) {
                    if (param_v_set_offs[i][j][pidx]) has_param_annot = 1;
                }
                if (has_param_annot) {
                    align_4(&b);
                    param_v_ref_list_offs[i][j] = b.len;
                    annot_set_ref_list_count++;
                    if (first_annot_set_ref_list_off == 0) first_annot_set_ref_list_off = b.len;
                    buf_write_u32(&b, c->virtual_methods[j].param_name_count);
                    for (uint32_t pidx = 0; pidx < c->virtual_methods[j].param_name_count; pidx++) {
                        buf_write_u32(&b, param_v_set_offs[i][j][pidx]);
                    }
                }
            }
        }
    }
    
    // Pass 3: Write all annotations_directory_item
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        int has_any = 0;
        if (class_annot_item_offs[i]) has_any = 1;
        
        uint32_t field_annot_count = 0;
        for (uint32_t j = 0; j < c->static_field_count; j++) if (field_s_set_offs[i][j]) field_annot_count++;
        for (uint32_t j = 0; j < c->instance_field_count; j++) if (field_i_set_offs[i][j]) field_annot_count++;
        if (field_annot_count > 0) has_any = 1;

        uint32_t meth_annot_count = 0;
        for (uint32_t j = 0; j < c->direct_method_count; j++) if (meth_d_set_offs[i][j]) meth_annot_count++;
        for (uint32_t j = 0; j < c->virtual_method_count; j++) if (meth_v_set_offs[i][j]) meth_annot_count++;
        if (meth_annot_count > 0) has_any = 1;

        uint32_t param_annot_count = 0;
        for (uint32_t j = 0; j < c->direct_method_count; j++) if (param_d_ref_list_offs[i][j]) param_annot_count++;
        for (uint32_t j = 0; j < c->virtual_method_count; j++) if (param_v_ref_list_offs[i][j]) param_annot_count++;
        if (param_annot_count > 0) has_any = 1;
        
        if (!has_any) continue;
        
        align_4(&b);
        annot_offsets[i] = b.len;
        annot_dir_count++;
        if (first_annot_dir_off == 0) first_annot_dir_off = b.len;
        
        buf_write_u32(&b, class_set_offs[i]);
        buf_write_u32(&b, field_annot_count);
        buf_write_u32(&b, meth_annot_count);
        buf_write_u32(&b, param_annot_count);
        
        if (field_annot_count > 0) {
            for (uint32_t j = 0; j < c->static_field_count; j++) {
                if (field_s_set_offs[i][j]) {
                    char mkey[1024];
                    snprintf(mkey, sizeof(mkey), "%s->%s:%s", c->descriptor, c->static_fields[j].name, c->static_fields[j].type);
                    uint32_t fidx = smali_pool_find(&ctx->fields, mkey);
                    buf_write_u32(&b, fidx == 0xFFFFFFFF ? 0 : fidx);
                    buf_write_u32(&b, field_s_set_offs[i][j]);
                }
            }
            for (uint32_t j = 0; j < c->instance_field_count; j++) {
                if (field_i_set_offs[i][j]) {
                    char mkey[1024];
                    snprintf(mkey, sizeof(mkey), "%s->%s:%s", c->descriptor, c->instance_fields[j].name, c->instance_fields[j].type);
                    uint32_t fidx = smali_pool_find(&ctx->fields, mkey);
                    buf_write_u32(&b, fidx == 0xFFFFFFFF ? 0 : fidx);
                    buf_write_u32(&b, field_i_set_offs[i][j]);
                }
            }
        }
        
        if (meth_annot_count > 0) {
            for (uint32_t j = 0; j < c->direct_method_count; j++) {
                if (meth_d_set_offs[i][j]) {
                    char mkey[1024];
                    snprintf(mkey, sizeof(mkey), "%s->%s%s", c->descriptor, c->direct_methods[j].name, c->direct_methods[j].signature);
                    uint32_t midx = smali_pool_find(&ctx->methods, mkey);
                    buf_write_u32(&b, midx == 0xFFFFFFFF ? 0 : midx);
                    buf_write_u32(&b, meth_d_set_offs[i][j]);
                }
            }
            for (uint32_t j = 0; j < c->virtual_method_count; j++) {
                if (meth_v_set_offs[i][j]) {
                    char mkey[1024];
                    snprintf(mkey, sizeof(mkey), "%s->%s%s", c->descriptor, c->virtual_methods[j].name, c->virtual_methods[j].signature);
                    uint32_t midx = smali_pool_find(&ctx->methods, mkey);
                    buf_write_u32(&b, midx == 0xFFFFFFFF ? 0 : midx);
                    buf_write_u32(&b, meth_v_set_offs[i][j]);
                }
            }
        }
        
        if (param_annot_count > 0) {
            for (uint32_t j = 0; j < c->direct_method_count; j++) {
                if (param_d_ref_list_offs[i][j]) {
                    char mkey[1024];
                    snprintf(mkey, sizeof(mkey), "%s->%s%s", c->descriptor, c->direct_methods[j].name, c->direct_methods[j].signature);
                    uint32_t midx = smali_pool_find(&ctx->methods, mkey);
                    buf_write_u32(&b, midx == 0xFFFFFFFF ? 0 : midx);
                    buf_write_u32(&b, param_d_ref_list_offs[i][j]);
                }
            }
            for (uint32_t j = 0; j < c->virtual_method_count; j++) {
                if (param_v_ref_list_offs[i][j]) {
                    char mkey[1024];
                    snprintf(mkey, sizeof(mkey), "%s->%s%s", c->descriptor, c->virtual_methods[j].name, c->virtual_methods[j].signature);
                    uint32_t midx = smali_pool_find(&ctx->methods, mkey);
                    buf_write_u32(&b, midx == 0xFFFFFFFF ? 0 : midx);
                    buf_write_u32(&b, param_v_ref_list_offs[i][j]);
                }
            }
        }
    }
    
    // Cleanup memory
    for (uint32_t i = 0; i < class_count; i++) {
        if (class_annot_item_offs[i]) free(class_annot_item_offs[i]);
        
        if (meth_d_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].direct_method_count; j++) if (meth_d_annot_item_offs[i][j]) free(meth_d_annot_item_offs[i][j]);
            free(meth_d_annot_item_offs[i]);
        }
        if (meth_v_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].virtual_method_count; j++) if (meth_v_annot_item_offs[i][j]) free(meth_v_annot_item_offs[i][j]);
            free(meth_v_annot_item_offs[i]);
        }
        if (meth_d_set_offs[i]) free(meth_d_set_offs[i]);
        if (meth_v_set_offs[i]) free(meth_v_set_offs[i]);

        if (field_s_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].static_field_count; j++) if (field_s_annot_item_offs[i][j]) free(field_s_annot_item_offs[i][j]);
            free(field_s_annot_item_offs[i]);
        }
        if (field_i_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].instance_field_count; j++) if (field_i_annot_item_offs[i][j]) free(field_i_annot_item_offs[i][j]);
            free(field_i_annot_item_offs[i]);
        }
        if (field_s_set_offs[i]) free(field_s_set_offs[i]);
        if (field_i_set_offs[i]) free(field_i_set_offs[i]);

        if (param_d_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].direct_method_count; j++) {
                if (param_d_annot_item_offs[i][j]) {
                    for (uint32_t pidx = 0; pidx < ctx->classes[i].direct_methods[j].param_name_count; pidx++) {
                        if (param_d_annot_item_offs[i][j][pidx]) free(param_d_annot_item_offs[i][j][pidx]);
                    }
                    free(param_d_annot_item_offs[i][j]);
                }
            }
            free(param_d_annot_item_offs[i]);
        }
        if (param_v_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].virtual_method_count; j++) {
                if (param_v_annot_item_offs[i][j]) {
                    for (uint32_t pidx = 0; pidx < ctx->classes[i].virtual_methods[j].param_name_count; pidx++) {
                        if (param_v_annot_item_offs[i][j][pidx]) free(param_v_annot_item_offs[i][j][pidx]);
                    }
                    free(param_v_annot_item_offs[i][j]);
                }
            }
            free(param_v_annot_item_offs[i]);
        }
        if (param_d_set_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].direct_method_count; j++) if (param_d_set_offs[i][j]) free(param_d_set_offs[i][j]);
            free(param_d_set_offs[i]);
        }
        if (param_v_set_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].virtual_method_count; j++) if (param_v_set_offs[i][j]) free(param_v_set_offs[i][j]);
            free(param_v_set_offs[i]);
        }
        if (param_d_ref_list_offs[i]) free(param_d_ref_list_offs[i]);
        if (param_v_ref_list_offs[i]) free(param_v_ref_list_offs[i]);
    }
    free(class_annot_item_offs);
    free(class_set_offs);
    free(meth_d_annot_item_offs);
    free(meth_v_annot_item_offs);
    free(meth_d_set_offs);
    free(meth_v_set_offs);
    free(field_s_annot_item_offs);
    free(field_i_annot_item_offs);
    free(field_s_set_offs);
    free(field_i_set_offs);
    free(param_d_annot_item_offs);
    free(param_v_annot_item_offs);
    free(param_d_set_offs);
    free(param_v_set_offs);
    free(param_d_ref_list_offs);
    free(param_v_ref_list_offs);
"""

lines[start_idx:end_idx] = [new_logic + "\n"]

with open('src/android/smali/smali_writer.c', 'w') as f:
    f.writelines(lines)

print("Updated successfully!")
