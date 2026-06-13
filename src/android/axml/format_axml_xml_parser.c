#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "format_axml_xml_parser.h"

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static int is_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == ':';
}

static char* parse_name(const char **p) {
    skip_ws(p);
    const char *start = *p;
    while (**p && is_name_char(**p)) (*p)++;
    if (start == *p) return NULL;
    
    size_t len = *p - start;
    char *name = malloc(len + 1);
    memcpy(name, start, len);
    name[len] = '\0';
    return name;
}

// Splits "android:name" into ns="android", name="name"
static void split_ns(char *full, char **ns, char **name) {
    char *colon = strchr(full, ':');
    if (colon) {
        *ns = malloc(colon - full + 1);
        memcpy(*ns, full, colon - full);
        (*ns)[colon - full] = '\0';
        
        *name = strdup(colon + 1);
        free(full);
    } else {
        *ns = NULL;
        *name = full;
    }
}

static char* parse_string(const char **p) {
    skip_ws(p);
    if (**p != '"' && **p != '\'') return NULL;
    char quote = *(*p)++;
    const char *start = *p;
    while (**p && **p != quote) (*p)++;
    
    size_t len = *p - start;
    char *val = malloc(len + 1);
    memcpy(val, start, len);
    val[len] = '\0';
    
    if (**p == quote) (*p)++;
    return val;
}

static xml_node* parse_element(const char **p) {
    skip_ws(p);
    if (**p != '<') return NULL;
    if ((*p)[1] == '/' || (*p)[1] == '!' || (*p)[1] == '?') return NULL;
    
    (*p)++; // Skip '<'
    char *full_name = parse_name(p);
    if (!full_name) return NULL;
    
    xml_node *node = calloc(1, sizeof(xml_node));
    split_ns(full_name, &node->ns, &node->name);
    
    xml_attr **last_attr = &node->attrs;
    
    // Parse attributes
    while (1) {
        skip_ws(p);
        if (**p == '>' || **p == '/' || **p == '\0') break;
        
        char *attr_full = parse_name(p);
        if (!attr_full) break; // Error or unexpected char
        
        skip_ws(p);
        char *val = NULL;
        if (**p == '=') {
            (*p)++; // Skip '='
            val = parse_string(p);
        } else {
            val = strdup(""); // Empty value
        }
        
        xml_attr *attr = calloc(1, sizeof(xml_attr));
        split_ns(attr_full, &attr->ns, &attr->name);
        attr->value = val;
        
        *last_attr = attr;
        last_attr = &attr->next;
    }
    
    skip_ws(p);
    if (**p == '/') {
        // Self-closing
        (*p)++;
        if (**p == '>') (*p)++;
        return node;
    }
    
    if (**p == '>') {
        (*p)++;
        // Parse children
        xml_node **last_child = &node->children;
        while (1) {
            skip_ws(p);
            if (**p == '<' && (*p)[1] == '/') {
                // End tag
                (*p) += 2;
                char *end_name = parse_name(p);
                if (end_name) free(end_name);
                skip_ws(p);
                if (**p == '>') (*p)++;
                break;
            } else if (**p == '<' && (*p)[1] == '!') {
                // Comment <!...>
                (*p) += 2;
                while (**p && !(**p == '>' && (*p)[-1] == '-' && (*p)[-2] == '-')) (*p)++;
                if (**p == '>') (*p)++;
                continue;
            }
            
            xml_node *child = parse_element(p);
            if (!child) {
                // Text node or error - skip until '<'
                while (**p && **p != '<') (*p)++;
                if (**p == '\0') break;
                continue;
            }
            
            *last_child = child;
            last_child = &child->next;
        }
    }
    
    return node;
}

xml_doc *xml_parse(const char *xml_text) {
    const char *p = xml_text;
    
    // Skip XML declaration <?xml ... ?>
    skip_ws(&p);
    if (strncmp(p, "<?xml", 5) == 0) {
        while (*p && !(*p == '?' && *(p+1) == '>')) p++;
        if (*p) p += 2;
    }
    
    // Skip comments
    while (1) {
        skip_ws(&p);
        if (strncmp(p, "<!--", 4) == 0) {
            while (*p && !(*p == '>' && *(p-1) == '-' && *(p-2) == '-')) p++;
            if (*p) p++;
        } else {
            break;
        }
    }
    
    xml_node *root = parse_element(&p);
    if (!root) return NULL;
    
    xml_doc *doc = calloc(1, sizeof(xml_doc));
    doc->root = root;
    return doc;
}

static void free_attrs(xml_attr *attr) {
    while (attr) {
        xml_attr *next = attr->next;
        if (attr->ns) free(attr->ns);
        if (attr->name) free(attr->name);
        if (attr->value) free(attr->value);
        free(attr);
        attr = next;
    }
}

static void free_nodes(xml_node *node) {
    while (node) {
        xml_node *next = node->next;
        if (node->ns) free(node->ns);
        if (node->name) free(node->name);
        free_attrs(node->attrs);
        free_nodes(node->children);
        free(node);
        node = next;
    }
}

void xml_free(xml_doc *doc) {
    if (!doc) return;
    free_nodes(doc->root);
    free(doc);
}
