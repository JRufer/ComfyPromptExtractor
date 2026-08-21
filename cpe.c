/**
 * ComfyPromptExtractor (CPE)
 * Ultra-fast, zero-dependency ComfyUI prompt & workflow metadata extractor for Linux.
 * 
 * Tech Stack:
 *  - C99 standard
 *  - Raylib (minimal GUI)
 *  - Native PNG chunk parser (skips IDAT pixel data for sub-millisecond parsing)
 *  - Embedded lightweight JSON parser for extracting clean prompt text
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <raylib.h>

#define CPE_VERSION "1.1.0"
#define WIN_WIDTH 900
#define WIN_HEIGHT 600
#define PADDING 20
#define HEADER_HEIGHT 60
#define FOOTER_HEIGHT 60

/* --- Data Structures --- */

typedef enum {
    PNG_OK = 0,
    PNG_ERR_IO,
    PNG_ERR_NOT_PNG,
    PNG_ERR_NO_METADATA
} PngStatus;

typedef enum {
    TARGET_PROMPT_TEXT = 0, /* Default: Clean extracted positive prompt text */
    TARGET_NEGATIVE_TEXT,   /* Clean extracted negative prompt text */
    TARGET_RAW_PROMPT_JSON, /* Raw ComfyUI prompt graph JSON */
    TARGET_WORKFLOW_JSON    /* Raw ComfyUI workflow JSON */
} OutputTarget;

typedef struct {
    /* Raw metadata chunks */
    char *raw_prompt;
    size_t raw_prompt_len;
    char *raw_workflow;
    size_t raw_workflow_len;
    char *raw_parameters;
    size_t raw_parameters_len;

    /* Extracted clean prompt texts */
    char *prompt_text;
    size_t prompt_text_len;
    char *negative_text;
    size_t negative_text_len;

    /* Active selection */
    const char *selected_label;
    char *selected_val;
    size_t selected_len;
    bool found;
    PngStatus status;
} MetadataResult;

typedef struct {
    char **lines;
    int count;
    int capacity;
} WrappedLines;

/* --- Miniature JSON Parser for Prompt Extraction --- */

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonNode {
    JsonType type;
    char *key;
    char *val_str;
    double val_num;
    bool val_bool;
    struct JsonNode *child;
    struct JsonNode *next;
} JsonNode;

static void json_free(JsonNode *node) {
    if (!node) return;
    if (node->key) free(node->key);
    if (node->val_str) free(node->val_str);
    JsonNode *c = node->child;
    while (c) {
        JsonNode *next = c->next;
        json_free(c);
        c = next;
    }
    free(node);
}

static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static char *parse_string_val(const char **p) {
    if (**p != '"') return NULL;
    (*p)++;
    size_t cap = 256;
    size_t len = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;

    while (**p && **p != '"') {
        char c = **p;
        if (c == '\\') {
            (*p)++;
            if (!**p) break;
            c = **p;
            switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '/': c = '/'; break;
                default: break;
            }
        }
        if (len + 2 >= cap) {
            cap *= 2;
            char *new_out = (char *)realloc(out, cap);
            if (!new_out) {
                free(out);
                return NULL;
            }
            out = new_out;
        }
        out[len++] = c;
        (*p)++;
    }
    if (**p == '"') (*p)++;
    out[len] = '\0';
    return out;
}

static JsonNode *parse_value(const char **p);

static JsonNode *parse_object(const char **p) {
    if (**p != '{') return NULL;
    (*p)++;
    JsonNode *node = (JsonNode *)calloc(1, sizeof(JsonNode));
    if (!node) return NULL;
    node->type = JSON_OBJECT;
    JsonNode **tail = &node->child;

    *p = skip_ws(*p);
    if (**p == '}') {
        (*p)++;
        return node;
    }

    while (**p) {
        *p = skip_ws(*p);
        if (**p != '"') break;
        char *key = parse_string_val(p);
        *p = skip_ws(*p);
        if (**p == ':') (*p)++;
        *p = skip_ws(*p);
        JsonNode *val = parse_value(p);
        if (val) {
            val->key = key;
            *tail = val;
            tail = &val->next;
        } else {
            if (key) free(key);
        }
        *p = skip_ws(*p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == '}') {
            (*p)++;
            break;
        }
    }
    return node;
}

static JsonNode *parse_array(const char **p) {
    if (**p != '[') return NULL;
    (*p)++;
    JsonNode *node = (JsonNode *)calloc(1, sizeof(JsonNode));
    if (!node) return NULL;
    node->type = JSON_ARRAY;
    JsonNode **tail = &node->child;

    *p = skip_ws(*p);
    if (**p == ']') {
        (*p)++;
        return node;
    }

    while (**p) {
        *p = skip_ws(*p);
        JsonNode *val = parse_value(p);
        if (val) {
            *tail = val;
            tail = &val->next;
        }
        *p = skip_ws(*p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == ']') {
            (*p)++;
            break;
        }
    }
    return node;
}

static JsonNode *parse_value(const char **p) {
    *p = skip_ws(*p);
    if (!**p) return NULL;

    if (**p == '{') return parse_object(p);
    if (**p == '[') return parse_array(p);
    if (**p == '"') {
        JsonNode *n = (JsonNode *)calloc(1, sizeof(JsonNode));
        if (!n) return NULL;
        n->type = JSON_STRING;
        n->val_str = parse_string_val(p);
        return n;
    }
    if (**p == 't' && strncmp(*p, "true", 4) == 0) {
        *p += 4;
        JsonNode *n = (JsonNode *)calloc(1, sizeof(JsonNode));
        if (!n) return NULL;
        n->type = JSON_BOOL;
        n->val_bool = true;
        return n;
    }
    if (**p == 'f' && strncmp(*p, "false", 5) == 0) {
        *p += 5;
        JsonNode *n = (JsonNode *)calloc(1, sizeof(JsonNode));
        if (!n) return NULL;
        n->type = JSON_BOOL;
        n->val_bool = false;
        return n;
    }
    if (**p == 'n' && strncmp(*p, "null", 4) == 0) {
        *p += 4;
        JsonNode *n = (JsonNode *)calloc(1, sizeof(JsonNode));
        if (!n) return NULL;
        n->type = JSON_NULL;
        return n;
    }
    /* Number */
    char *endptr;
    double d = strtod(*p, &endptr);
    if (endptr != *p) {
        *p = endptr;
        JsonNode *n = (JsonNode *)calloc(1, sizeof(JsonNode));
        if (!n) return NULL;
        n->type = JSON_NUMBER;
        n->val_num = d;
        return n;
    }
    return NULL;
}

static JsonNode *json_get(JsonNode *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (JsonNode *c = obj->child; c; c = c->next) {
        if (c->key && strcmp(c->key, key) == 0) return c;
    }
    return NULL;
}

static const char *json_get_str(JsonNode *obj, const char *key) {
    JsonNode *n = json_get(obj, key);
    if (n && n->type == JSON_STRING) return n->val_str;
    return NULL;
}

/* --- Prompt Text Extractor --- */

/* --- Helper Utilities for Prompt Filtering & Graph Traversal --- */

static bool json_get_bool_val(JsonNode *root, JsonNode *inputs, const char *key, bool default_val) {
    if (!inputs) return default_val;
    JsonNode *n = json_get(inputs, key);
    if (!n) return default_val;
    if (n->type == JSON_BOOL) return n->val_bool;
    if (n->type == JSON_ARRAY && n->child) {
        const char *src_id = (n->child->type == JSON_STRING) ? n->child->val_str : NULL;
        if (src_id) {
            JsonNode *src_node = json_get(root, src_id);
            if (src_node) {
                JsonNode *src_inputs = json_get(src_node, "inputs");
                if (src_inputs) {
                    JsonNode *val_node = json_get(src_inputs, "value");
                    if (val_node && val_node->type == JSON_BOOL) return val_node->val_bool;
                }
            }
        }
    }
    return default_val;
}

static bool is_system_prompt_str(const char *s) {
    if (!s) return false;
    if (strstr(s, "FORGET EVERYTHING YOU KNOW") ||
        strstr(s, "You are an expert prompt engineer") ||
        strstr(s, "System Prompt") ||
        strstr(s, "Faithfulness First:")) {
        return true;
    }
    return false;
}

static char *extract_text_from_node(JsonNode *root, JsonNode *node, int depth) {
    if (!node || depth > 15) return NULL;
    const char *ctype = json_get_str(node, "class_type");
    JsonNode *inputs = json_get(node, "inputs");
    if (!inputs) return NULL;

    /* 1. ComfySwitchNode / Switch / IfElse / Select */
    if (ctype && (strstr(ctype, "Switch") || strstr(ctype, "IfElse") || strstr(ctype, "Select"))) {
        bool sw_val = json_get_bool_val(root, inputs, "switch", false);
        const char *link_key = sw_val ? "on_true" : "on_false";
        JsonNode *sw_link = json_get(inputs, link_key);
        if (!sw_link) sw_link = json_get(inputs, sw_val ? "input_b" : "input_a");
        if (sw_link && sw_link->type == JSON_ARRAY && sw_link->child) {
            const char *src_id = (sw_link->child->type == JSON_STRING) ? sw_link->child->val_str : NULL;
            if (src_id) {
                JsonNode *src_node = json_get(root, src_id);
                if (src_node) return extract_text_from_node(root, src_node, depth + 1);
            }
        }
    }

    /* 2. PreviewAny / Reroute / PassThrough */
    if (ctype && (strstr(ctype, "Preview") || strstr(ctype, "Reroute") || strstr(ctype, "PassThrough"))) {
        JsonNode *src_link = json_get(inputs, "source");
        if (!src_link) src_link = json_get(inputs, "input");
        if (src_link && src_link->type == JSON_ARRAY && src_link->child) {
            const char *src_id = (src_link->child->type == JSON_STRING) ? src_link->child->val_str : NULL;
            if (src_id) {
                JsonNode *src_node = json_get(root, src_id);
                if (src_node) return extract_text_from_node(root, src_node, depth + 1);
            }
        }
    }

    /* 3. StringConcatenate / TextConcatenate */
    if (ctype && (strstr(ctype, "StringConcatenate") || strstr(ctype, "TextConcatenate") || strstr(ctype, "Concat"))) {
        char *str_a = NULL;
        char *str_b = NULL;

        JsonNode *link_a = json_get(inputs, "string_a");
        if (!link_a) link_a = json_get(inputs, "text1");
        if (link_a) {
            if (link_a->type == JSON_STRING) str_a = strdup(link_a->val_str);
            else if (link_a->type == JSON_ARRAY && link_a->child) {
                const char *src_id = (link_a->child->type == JSON_STRING) ? link_a->child->val_str : NULL;
                if (src_id) {
                    JsonNode *src_node = json_get(root, src_id);
                    if (src_node) str_a = extract_text_from_node(root, src_node, depth + 1);
                }
            }
        }

        JsonNode *link_b = json_get(inputs, "string_b");
        if (!link_b) link_b = json_get(inputs, "text2");
        if (link_b) {
            if (link_b->type == JSON_STRING) str_b = strdup(link_b->val_str);
            else if (link_b->type == JSON_ARRAY && link_b->child) {
                const char *src_id = (link_b->child->type == JSON_STRING) ? link_b->child->val_str : NULL;
                if (src_id) {
                    JsonNode *src_node = json_get(root, src_id);
                    if (src_node) str_b = extract_text_from_node(root, src_node, depth + 1);
                }
            }
        }

        /* Filter out system prompts */
        if (str_a && is_system_prompt_str(str_a)) {
            free(str_a);
            str_a = NULL;
        }
        if (str_b && is_system_prompt_str(str_b)) {
            free(str_b);
            str_b = NULL;
        }

        if (str_a && str_b) {
            const char *delim = json_get_str(inputs, "delimiter");
            if (!delim) delim = "";
            size_t len = strlen(str_a) + strlen(delim) + strlen(str_b) + 1;
            char *combined = (char *)malloc(len);
            if (combined) {
                snprintf(combined, len, "%s%s%s", str_a, delim, str_b);
                free(str_a);
                free(str_b);
                return combined;
            }
        }
        if (str_a) return str_a;
        if (str_b) return str_b;
    }

    /* 4. Direct inputs.text as string or link */
    JsonNode *n_text = json_get(inputs, "text");
    if (n_text) {
        if (n_text->type == JSON_STRING && n_text->val_str && n_text->val_str[0] != '\0') {
            if (!is_system_prompt_str(n_text->val_str)) return strdup(n_text->val_str);
        }
        if (n_text->type == JSON_ARRAY && n_text->child) {
            const char *src_id = (n_text->child->type == JSON_STRING) ? n_text->child->val_str : NULL;
            if (src_id) {
                JsonNode *src_node = json_get(root, src_id);
                if (src_node) {
                    char *res = extract_text_from_node(root, src_node, depth + 1);
                    if (res) return res;
                }
            }
        }
    }

    /* 5. SDXL text_g and text_l */
    const char *text_g = json_get_str(inputs, "text_g");
    const char *text_l = json_get_str(inputs, "text_l");
    if (text_g || text_l) {
        if (text_g && text_l) {
            if (strcmp(text_g, text_l) == 0) return strdup(text_g);
            if (strstr(text_l, text_g)) return strdup(text_l);
            if (strstr(text_g, text_l)) return strdup(text_g);
            size_t len = strlen(text_g) + strlen(text_l) + 4;
            char *combined = (char *)malloc(len);
            if (combined) {
                snprintf(combined, len, "%s\n%s", text_g, text_l);
                return combined;
            }
        }
        return strdup(text_g ? text_g : text_l);
    }

    /* 6. Flux clip_l and t5xxl */
    const char *clip_l = json_get_str(inputs, "clip_l");
    const char *t5xxl = json_get_str(inputs, "t5xxl");
    if (clip_l || t5xxl) {
        if (clip_l && t5xxl) {
            if (strcmp(clip_l, t5xxl) == 0) return strdup(clip_l);
            if (strstr(t5xxl, clip_l)) return strdup(t5xxl);
            if (strstr(clip_l, t5xxl)) return strdup(clip_l);
            size_t len = strlen(clip_l) + strlen(t5xxl) + 4;
            char *combined = (char *)malloc(len);
            if (combined) {
                snprintf(combined, len, "%s\n%s", clip_l, t5xxl);
                return combined;
            }
        }
        return strdup(clip_l ? clip_l : t5xxl);
    }

    /* 7. Primitive / TextBox value, string, prompt */
    const char *val = json_get_str(inputs, "value");
    if (val && val[0] != '\0' && !is_system_prompt_str(val)) return strdup(val);
    const char *str = json_get_str(inputs, "string");
    if (str && str[0] != '\0' && !is_system_prompt_str(str)) return strdup(str);
    const char *p_field = json_get_str(inputs, "prompt");
    if (p_field && p_field[0] != '\0' && !is_system_prompt_str(p_field)) return strdup(p_field);

    /* 8. Intermediate conditioning node (FluxGuidance, ConditioningCombine, ConditioningSetArea, LoraLoader, etc.) */
    const char *cond_keys[] = { "conditioning", "conditioning_1", "conditioning_to", "conditioning_from", "cond", "clip", NULL };
    for (int i = 0; cond_keys[i]; i++) {
        JsonNode *c_link = json_get(inputs, cond_keys[i]);
        if (c_link && c_link->type == JSON_ARRAY && c_link->child) {
            const char *src_id = (c_link->child->type == JSON_STRING) ? c_link->child->val_str : NULL;
            if (src_id) {
                JsonNode *src_node = json_get(root, src_id);
                if (src_node) {
                    char *res = extract_text_from_node(root, src_node, depth + 1);
                    if (res) return res;
                }
            }
        }
    }

    return NULL;
}

static void extract_comfyui_prompts_from_json(const char *prompt_json, char **out_pos, char **out_neg) {
    if (!prompt_json || !out_pos || !out_neg) return;
    *out_pos = NULL;
    *out_neg = NULL;

    const char *p = prompt_json;
    JsonNode *root = parse_value(&p);
    if (!root || root->type != JSON_OBJECT) {
        if (root) json_free(root);
        return;
    }

    /* 1. First priority: Check if any node is explicitly titled "User Prompt" or "User Text" */
    for (JsonNode *node = root->child; node; node = node->next) {
        JsonNode *meta = json_get(node, "_meta");
        if (meta) {
            const char *title = json_get_str(meta, "title");
            if (title && (strstr(title, "User Prompt") || strstr(title, "User Text"))) {
                JsonNode *inputs = json_get(node, "inputs");
                if (inputs) {
                    const char *val = json_get_str(inputs, "value");
                    if (!val) val = json_get_str(inputs, "string");
                    if (!val) val = json_get_str(inputs, "text");
                    if (val && val[0] != '\0' && !is_system_prompt_str(val)) {
                        *out_pos = strdup(val);
                        break;
                    }
                }
            }
        }
    }

    /* 2. Trace Sampler nodes for positive and negative connections */
    for (JsonNode *node = root->child; node; node = node->next) {
        const char *ctype = json_get_str(node, "class_type");
        if (ctype && strstr(ctype, "Sampler")) {
            JsonNode *inputs = json_get(node, "inputs");
            if (!inputs) continue;

            /* Positive link */
            if (!*out_pos) {
                JsonNode *pos = json_get(inputs, "positive");
                if (pos && pos->type == JSON_ARRAY && pos->child && pos->child->type == JSON_STRING) {
                    JsonNode *pos_node = json_get(root, pos->child->val_str);
                    if (pos_node) {
                        *out_pos = extract_text_from_node(root, pos_node, 0);
                    }
                }
            }

            /* Negative link */
            if (!*out_neg) {
                JsonNode *neg = json_get(inputs, "negative");
                if (neg && neg->type == JSON_ARRAY && neg->child && neg->child->type == JSON_STRING) {
                    JsonNode *neg_node = json_get(root, neg->child->val_str);
                    if (neg_node) {
                        *out_neg = extract_text_from_node(root, neg_node, 0);
                    }
                }
            }
        }
    }

    /* 3. Fallback: if positive prompt was not found by tracing sampler, scan text encode nodes */
    if (!*out_pos) {
        for (JsonNode *node = root->child; node; node = node->next) {
            const char *ctype = json_get_str(node, "class_type");
            if (ctype && (strstr(ctype, "CLIPTextEncode") || strstr(ctype, "TextEncode") || strstr(ctype, "Prompt"))) {
                char *txt = extract_text_from_node(root, node, 0);
                if (txt && txt[0] != '\0') {
                    if (*out_neg && strcmp(txt, *out_neg) == 0) {
                        free(txt);
                        continue;
                    }
                    *out_pos = txt;
                    break;
                }
            }
        }
    }

    json_free(root);
}

static void extract_comfyui_prompts_from_workflow(const char *workflow_json, char **out_pos, char **out_neg) {
    if (!workflow_json || !out_pos || !out_neg) return;
    *out_pos = NULL;
    *out_neg = NULL;

    const char *p = workflow_json;
    JsonNode *root = parse_value(&p);
    if (!root) return;

    JsonNode *nodes = json_get(root, "nodes");
    if (nodes && nodes->type == JSON_ARRAY) {
        for (JsonNode *node = nodes->child; node; node = node->next) {
            const char *type = json_get_str(node, "type");
            const char *title = json_get_str(node, "title");
            if (type && (strstr(type, "CLIPTextEncode") || strstr(type, "TextEncode") || strstr(type, "Prompt"))) {
                JsonNode *wvals = json_get(node, "widgets_values");
                if (wvals && wvals->type == JSON_ARRAY && wvals->child) {
                    for (JsonNode *val = wvals->child; val; val = val->next) {
                        if (val->type == JSON_STRING && val->val_str && val->val_str[0] != '\0') {
                            bool is_neg = (title && strstr(title, "Negative")) || (type && strstr(type, "Negative"));
                            if (is_neg && !*out_neg) {
                                *out_neg = strdup(val->val_str);
                            } else if (!is_neg && !*out_pos) {
                                *out_pos = strdup(val->val_str);
                            }
                        }
                    }
                }
            }
        }
    }

    json_free(root);
}

static void extract_a1111_prompts(const char *params, char **out_pos, char **out_neg) {
    if (!params || !out_pos || !out_neg) return;
    *out_pos = NULL;
    *out_neg = NULL;

    const char *neg_marker = "\nNegative prompt: ";
    const char *steps_marker = "\nSteps: ";

    const char *neg_pos = strstr(params, neg_marker);
    const char *steps_pos = strstr(params, steps_marker);

    /* Positive prompt */
    const char *pos_end = neg_pos ? neg_pos : (steps_pos ? steps_pos : params + strlen(params));
    size_t pos_len = pos_end - params;
    if (pos_len > 0) {
        char *pos_str = (char *)malloc(pos_len + 1);
        if (pos_str) {
            memcpy(pos_str, params, pos_len);
            pos_str[pos_len] = '\0';
            *out_pos = pos_str;
        }
    }

    /* Negative prompt */
    if (neg_pos) {
        const char *neg_start = neg_pos + strlen(neg_marker);
        const char *neg_end = steps_pos ? steps_pos : params + strlen(params);
        if (neg_end > neg_start) {
            size_t neg_len = neg_end - neg_start;
            char *neg_str = (char *)malloc(neg_len + 1);
            if (neg_str) {
                memcpy(neg_str, neg_start, neg_len);
                neg_str[neg_len] = '\0';
                *out_neg = neg_str;
            }
        }
    }
}

/* --- PNG Chunk Parsing Implementation --- */

static void free_metadata(MetadataResult *meta) {
    if (!meta) return;
    if (meta->raw_prompt) { free(meta->raw_prompt); meta->raw_prompt = NULL; }
    if (meta->raw_workflow) { free(meta->raw_workflow); meta->raw_workflow = NULL; }
    if (meta->raw_parameters) { free(meta->raw_parameters); meta->raw_parameters = NULL; }
    if (meta->prompt_text) { free(meta->prompt_text); meta->prompt_text = NULL; }
    if (meta->negative_text) { free(meta->negative_text); meta->negative_text = NULL; }
    meta->selected_label = NULL;
    meta->selected_val = NULL;
    meta->selected_len = 0;
    meta->found = false;
    meta->status = PNG_ERR_NO_METADATA;
}

static PngStatus parse_png_metadata(const char *filepath, MetadataResult *meta) {
    if (!filepath || !meta) return PNG_ERR_IO;
    memset(meta, 0, sizeof(MetadataResult));
    meta->status = PNG_ERR_NO_METADATA;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        meta->status = PNG_ERR_IO;
        return PNG_ERR_IO;
    }

    /* Verify 8-byte PNG header: \x89PNG\r\n\x1a\n */
    uint8_t sig[8];
    if (fread(sig, 1, 8, fp) != 8) {
        fclose(fp);
        meta->status = PNG_ERR_NOT_PNG;
        return PNG_ERR_NOT_PNG;
    }

    const uint8_t png_sig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    if (memcmp(sig, png_sig, 8) != 0) {
        fclose(fp);
        meta->status = PNG_ERR_NOT_PNG;
        return PNG_ERR_NOT_PNG;
    }

    /* Iterate through chunks */
    while (!feof(fp)) {
        uint8_t len_buf[4];
        if (fread(len_buf, 1, 4, fp) != 4) break;

        uint32_t chunk_len = ((uint32_t)len_buf[0] << 24) |
                             ((uint32_t)len_buf[1] << 16) |
                             ((uint32_t)len_buf[2] << 8)  |
                              (uint32_t)len_buf[3];

        char chunk_type[5] = {0};
        if (fread(chunk_type, 1, 4, fp) != 4) break;

        /* If we hit IEND, PNG ends */
        if (strcmp(chunk_type, "IEND") == 0) {
            break;
        }

        /* Parse tEXt chunk (Latin-1 / ASCII) */
        if (strcmp(chunk_type, "tEXt") == 0 && chunk_len > 0) {
            char *buf = (char *)malloc(chunk_len + 1);
            if (buf) {
                if (fread(buf, 1, chunk_len, fp) == chunk_len) {
                    buf[chunk_len] = '\0';
                    size_t klen = strnlen(buf, chunk_len);
                    if (klen < chunk_len) {
                        const char *key = buf;
                        const char *val = buf + klen + 1;
                        size_t vlen = chunk_len - (klen + 1);

                        if (strcmp(key, "prompt") == 0 && !meta->raw_prompt) {
                            meta->raw_prompt = (char *)malloc(vlen + 1);
                            if (meta->raw_prompt) {
                                memcpy(meta->raw_prompt, val, vlen);
                                meta->raw_prompt[vlen] = '\0';
                                meta->raw_prompt_len = vlen;
                            }
                        } else if (strcmp(key, "workflow") == 0 && !meta->raw_workflow) {
                            meta->raw_workflow = (char *)malloc(vlen + 1);
                            if (meta->raw_workflow) {
                                memcpy(meta->raw_workflow, val, vlen);
                                meta->raw_workflow[vlen] = '\0';
                                meta->raw_workflow_len = vlen;
                            }
                        } else if (strcmp(key, "parameters") == 0 && !meta->raw_parameters) {
                            meta->raw_parameters = (char *)malloc(vlen + 1);
                            if (meta->raw_parameters) {
                                memcpy(meta->raw_parameters, val, vlen);
                                meta->raw_parameters[vlen] = '\0';
                                meta->raw_parameters_len = vlen;
                            }
                        }
                    }
                }
                free(buf);
            } else {
                fseek(fp, chunk_len, SEEK_CUR);
            }
            /* Skip 4 bytes CRC */
            fseek(fp, 4, SEEK_CUR);
        }
        /* Parse iTXt chunk (International UTF-8 text) */
        else if (strcmp(chunk_type, "iTXt") == 0 && chunk_len > 5) {
            uint8_t *buf = (uint8_t *)malloc(chunk_len + 1);
            if (buf) {
                if (fread(buf, 1, chunk_len, fp) == chunk_len) {
                    buf[chunk_len] = '\0';
                    size_t klen = strnlen((char *)buf, chunk_len);
                    if (klen + 5 <= chunk_len) {
                        const char *key = (char *)buf;
                        uint8_t comp_flag = buf[klen + 1];
                        size_t idx = klen + 3;
                        
                        /* Skip lang tag */
                        while (idx < chunk_len && buf[idx] != 0) idx++;
                        idx++;
                        
                        /* Skip translated key */
                        while (idx < chunk_len && buf[idx] != 0) idx++;
                        idx++;
                        
                        if (idx <= chunk_len && comp_flag == 0) {
                            const char *val = (char *)(buf + idx);
                            size_t vlen = chunk_len - idx;

                            if (strcmp(key, "prompt") == 0 && !meta->raw_prompt) {
                                meta->raw_prompt = (char *)malloc(vlen + 1);
                                if (meta->raw_prompt) {
                                    memcpy(meta->raw_prompt, val, vlen);
                                    meta->raw_prompt[vlen] = '\0';
                                    meta->raw_prompt_len = vlen;
                                }
                            } else if (strcmp(key, "workflow") == 0 && !meta->raw_workflow) {
                                meta->raw_workflow = (char *)malloc(vlen + 1);
                                if (meta->raw_workflow) {
                                    memcpy(meta->raw_workflow, val, vlen);
                                    meta->raw_workflow[vlen] = '\0';
                                    meta->raw_workflow_len = vlen;
                                }
                            } else if (strcmp(key, "parameters") == 0 && !meta->raw_parameters) {
                                meta->raw_parameters = (char *)malloc(vlen + 1);
                                if (meta->raw_parameters) {
                                    memcpy(meta->raw_parameters, val, vlen);
                                    meta->raw_parameters[vlen] = '\0';
                                    meta->raw_parameters_len = vlen;
                                }
                            }
                        }
                    }
                }
                free(buf);
            } else {
                fseek(fp, chunk_len, SEEK_CUR);
            }
            /* Skip 4 bytes CRC */
            fseek(fp, 4, SEEK_CUR);
        }
        /* Skip all other chunks (IDAT pixel data, pHYs, etc.) */
        else {
            fseek(fp, chunk_len + 4, SEEK_CUR);
        }
    }

    fclose(fp);

    /* Extract prompt text from available metadata sources */
    if (meta->raw_prompt) {
        extract_comfyui_prompts_from_json(meta->raw_prompt, &meta->prompt_text, &meta->negative_text);
    }
    if (!meta->prompt_text && meta->raw_workflow) {
        extract_comfyui_prompts_from_workflow(meta->raw_workflow, &meta->prompt_text, &meta->negative_text);
    }
    if (!meta->prompt_text && meta->raw_parameters) {
        extract_a1111_prompts(meta->raw_parameters, &meta->prompt_text, &meta->negative_text);
    }

    if (meta->prompt_text) {
        meta->prompt_text_len = strlen(meta->prompt_text);
    }
    if (meta->negative_text) {
        meta->negative_text_len = strlen(meta->negative_text);
    }

    /* Check if anything was found */
    if (meta->prompt_text || meta->raw_prompt || meta->raw_workflow || meta->raw_parameters) {
        meta->found = true;
        meta->status = PNG_OK;
    } else {
        meta->found = false;
        meta->status = PNG_ERR_NO_METADATA;
    }

    return meta->status;
}

/* --- Clipboard Helper for Linux --- */

static void copy_to_clipboard(const char *text) {
    if (!text) return;
    SetClipboardText(text);

    /* For Linux desktop environments (Wayland / X11) ensure persistence on quick process termination */
    if (getenv("WAYLAND_DISPLAY")) {
        FILE *p = popen("wl-copy 2>/dev/null", "w");
        if (p) {
            fputs(text, p);
            pclose(p);
        }
    } else if (getenv("DISPLAY")) {
        FILE *p = popen("xclip -selection clipboard 2>/dev/null || xsel --clipboard 2>/dev/null", "w");
        if (p) {
            fputs(text, p);
            pclose(p);
        }
    }
}

static const char *get_filename_basename(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* --- Text Wrapping Utility --- */

static void add_wrapped_line(WrappedLines *wl, const char *start, size_t len) {
    if (!wl) return;
    if (wl->count >= wl->capacity) {
        wl->capacity = wl->capacity ? wl->capacity * 2 : 64;
        char **new_lines = (char **)realloc(wl->lines, wl->capacity * sizeof(char *));
        if (!new_lines) return;
        wl->lines = new_lines;
    }
    char *s = (char *)malloc(len + 1);
    if (s) {
        if (len > 0 && start) {
            memcpy(s, start, len);
        }
        s[len] = '\0';
        wl->lines[wl->count++] = s;
    }
}

static WrappedLines wrap_text(const char *text, Font font, float font_size, float spacing, float max_width) {
    WrappedLines wl;
    wl.count = 0;
    wl.capacity = 64;
    wl.lines = (char **)malloc(wl.capacity * sizeof(char *));

    if (!text || !wl.lines) {
        wl.count = 0;
        return wl;
    }

    const char *p = text;
    while (*p) {
        /* Find paragraph / physical newline */
        const char *line_start = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        size_t line_len = (size_t)(p - line_start);

        if (*p == '\r' && *(p + 1) == '\n') p += 2;
        else if (*p == '\n' || *p == '\r') p++;

        if (line_len == 0) {
            add_wrapped_line(&wl, "", 0);
            continue;
        }

        /* Wrap this line to fit within max_width */
        const char *cur = line_start;
        const char *end = line_start + line_len;

        while (cur < end) {
            const char *fit_end = cur;
            char measure_buf[512];

            while (fit_end < end) {
                /* Advance to next space or word boundary */
                const char *next = fit_end + 1;
                while (next < end && !isspace((unsigned char)*next)) next++;

                size_t test_len = (size_t)(next - cur);
                float measured_w = 0.0f;

                if (test_len < sizeof(measure_buf)) {
                    memcpy(measure_buf, cur, test_len);
                    measure_buf[test_len] = '\0';
                    measured_w = MeasureTextEx(font, measure_buf, font_size, spacing).x;
                } else {
                    measured_w = max_width + 10.0f;
                }

                if (measured_w > max_width) {
                    if (fit_end == cur) {
                        /* Single word exceeds line width: break by character */
                        while (fit_end < next) {
                            size_t c_len = (size_t)(fit_end + 1 - cur);
                            if (c_len < sizeof(measure_buf)) {
                                memcpy(measure_buf, cur, c_len);
                                measure_buf[c_len] = '\0';
                                if (MeasureTextEx(font, measure_buf, font_size, spacing).x > max_width && fit_end > cur) {
                                    break;
                                }
                            }
                            fit_end++;
                        }
                    }
                    break;
                }

                fit_end = next;
                if (fit_end < end && isspace((unsigned char)*fit_end)) {
                    fit_end++;
                }
            }

            if (fit_end <= cur) {
                fit_end = cur + 1;
            }

            size_t seg_len = (size_t)(fit_end - cur);
            while (seg_len > 0 && isspace((unsigned char)cur[seg_len - 1])) {
                seg_len--;
            }

            add_wrapped_line(&wl, cur, seg_len);
            cur = fit_end;
        }
    }

    return wl;
}

static void free_wrapped_lines(WrappedLines *wl) {
    if (!wl || !wl->lines) return;
    for (int i = 0; i < wl->count; i++) {
        if (wl->lines[i]) free(wl->lines[i]);
    }
    free(wl->lines);
    wl->lines = NULL;
    wl->count = 0;
    wl->capacity = 0;
}

/* --- OS System Font Resolver --- */

static char *get_system_font_path(void) {
    /* 1. Try fontconfig fc-match */
    FILE *fp = popen("fc-match -f \"%{file}\" 2>/dev/null", "r");
    if (fp) {
        char buf[512] = {0};
        if (fgets(buf, sizeof(buf), fp)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
            pclose(fp);
            if (len > 0 && access(buf, R_OK) == 0) {
                return strdup(buf);
            }
        } else {
            pclose(fp);
        }
    }

    /* 2. Fallback Linux system font paths */
    const char *fallback_paths[] = {
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/gnu-free/FreeSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/cantarell/Cantarell-Regular.otf",
        "/usr/share/fonts/ubuntu/Ubuntu-R.ttf",
        NULL
    };

    for (int i = 0; fallback_paths[i]; i++) {
        if (access(fallback_paths[i], R_OK) == 0) {
            return strdup(fallback_paths[i]);
        }
    }

    return NULL;
}

/*/* --- GUI Subsystem --- */

static void run_gui_mode(const char *filepath, MetadataResult *meta) {
    /* Enable High-DPI, MSAA, VSync */
    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    
    int win_w = 880;
    int win_h = 580;
    InitWindow(win_w, win_h, "ComfyPromptExtractor");
    SetTargetFPS(60);

    /* Load & Set Window Manager Icon */
    const char *home_dir = getenv("HOME");
    char user_icon_path[512] = {0};
    if (home_dir) {
        snprintf(user_icon_path, sizeof(user_icon_path), "%s/.local/share/icons/hicolor/256x256/apps/cpe.png", home_dir);
    }
    const char *icon_paths[] = {
        "cpe.png",
        user_icon_path,
        "/usr/local/share/icons/hicolor/256x256/apps/cpe.png",
        "/usr/share/icons/hicolor/256x256/apps/cpe.png",
        "/usr/local/share/pixmaps/cpe.png",
        "/usr/share/pixmaps/cpe.png",
        NULL
    };
    for (int i = 0; icon_paths[i]; i++) {
        if (icon_paths[i][0] != '\0' && access(icon_paths[i], R_OK) == 0) {
            Image app_icon = LoadImage(icon_paths[i]);
            if (app_icon.data != NULL) {
                SetWindowIcon(app_icon);
                UnloadImage(app_icon);
                break;
            }
        }
    }

    /* Get High-DPI scale for high-resolution TTF glyph rasterization */
    Vector2 dpi = GetWindowScaleDPI();
    float dpi_scale = (dpi.x > 1.0f) ? dpi.x : 1.0f;

    /* Center on monitor */
    int monitor = GetCurrentMonitor();
    int mon_w = GetMonitorWidth(monitor);
    int mon_h = GetMonitorHeight(monitor);
    if (mon_w > 0 && mon_h > 0) {
        SetWindowPosition((mon_w - win_w) / 2, (mon_h - win_h) / 2);
    }

    /* Load OS System Font at High-DPI rasterization size */
    char *sys_font_path = get_system_font_path();
    Font font;
    bool custom_font_loaded = false;
    float font_size = 19.0f;
    float font_spacing = 1.0f;
    float line_height = 27.0f;

    if (sys_font_path) {
        int raster_size = (int)(font_size * dpi_scale * 1.5f);
        if (raster_size < 32) raster_size = 32;
        font = LoadFontEx(sys_font_path, raster_size, NULL, 0);
        if (font.texture.id > 0) {
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            custom_font_loaded = true;
        } else {
            font = GetFontDefault();
        }
        free(sys_font_path);
    } else {
        font = GetFontDefault();
    }

    /* Logical Layout Metrics (880 x 580 canvas) */
    float padding = 20.0f;
    float header_height = 60.0f;
    float footer_height = 60.0f;
    float text_area_width = win_w - (padding * 2.0f) - 30.0f;

    const char *filename = get_filename_basename(filepath);
    const char *display_text = "(No prompt metadata found in image)";
    if (meta->status == PNG_OK && meta->selected_val) {
        display_text = meta->selected_val;
    } else if (meta->status == PNG_ERR_IO) {
        display_text = "(Error: Could not open or read image file)";
    } else if (meta->status == PNG_ERR_NOT_PNG) {
        display_text = "(Error: File is not a valid PNG image)";
    }

    WrappedLines wl = wrap_text(display_text, font, font_size, font_spacing, text_area_width);

    float scroll_y = 0.0f;
    float text_panel_height = win_h - header_height - footer_height - (padding * 2.0f);
    float total_content_height = wl.count * line_height;
    float max_scroll = (total_content_height > text_panel_height) ? (total_content_height - text_panel_height + 24.0f) : 0.0f;

    /* Theme Colors */
    Color bg_dark        = (Color){ 17, 20, 26, 255 };      /* Deep slate */
    Color header_bg      = (Color){ 24, 28, 36, 255 };      /* Header dark panel */
    Color panel_bg       = (Color){ 13, 15, 20, 255 };      /* Content panel */
    Color border_color   = (Color){ 42, 48, 60, 255 };      /* Border stroke */
    Color text_bright    = (Color){ 241, 245, 249, 255 };   /* Main text */
    Color text_muted     = (Color){ 148, 163, 184, 255 };   /* Secondary text */
    Color accent_indigo  = (Color){ 99, 102, 241, 255 };    /* Accent primary */
    Color accent_hover   = (Color){ 129, 140, 248, 255 };   /* Accent hover */
    Color accent_active  = (Color){ 79, 70, 229, 255 };     /* Accent pressed */
    Color btn_secondary  = (Color){ 39, 45, 56, 255 };      /* Close button */
    Color btn_sec_hover  = (Color){ 51, 60, 74, 255 };
    Color badge_bg       = (Color){ 34, 40, 52, 255 };
    Color badge_green    = (Color){ 16, 185, 129, 255 };

    Rectangle copy_btn_rect = { win_w - padding - 140, win_h - footer_height + 10, 140, 40 };
    Rectangle workflow_btn_rect = { win_w - padding - 140 - 10 - 150, win_h - footer_height + 10, 150, 40 };
    Rectangle close_btn_rect = { win_w - padding - 140 - 10 - 150 - 10 - 90, win_h - footer_height + 10, 90, 40 };
    Rectangle header_close_rect = { win_w - padding - 28, 16, 28, 28 };

    bool has_been_focused = false;
    int frame_count = 0;
    bool should_copy_and_exit = false;
    bool should_copy_wf_and_exit = false;
    bool wf_has_data = (meta->raw_workflow != NULL || meta->raw_prompt != NULL);

    while (!WindowShouldClose()) {
        frame_count++;

        if (IsWindowFocused()) {
            has_been_focused = true;
        }
        if (frame_count > 10 && has_been_focused && !IsWindowFocused()) {
            break;
        }

        bool ctrl_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if ((ctrl_down && IsKeyPressed(KEY_C)) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            should_copy_and_exit = true;
            break;
        }

        if (ctrl_down && IsKeyPressed(KEY_W)) {
            if (wf_has_data) {
                should_copy_wf_and_exit = true;
                break;
            }
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            break;
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            scroll_y -= wheel * (line_height * 3.0f);
        }
        if (IsKeyDown(KEY_UP)) scroll_y -= line_height;
        if (IsKeyDown(KEY_DOWN)) scroll_y += line_height;
        if (IsKeyPressed(KEY_PAGE_UP)) scroll_y -= text_panel_height;
        if (IsKeyPressed(KEY_PAGE_DOWN)) scroll_y += text_panel_height;
        if (IsKeyPressed(KEY_HOME)) scroll_y = 0.0f;
        if (IsKeyPressed(KEY_END)) scroll_y = max_scroll;

        if (scroll_y < 0.0f) scroll_y = 0.0f;
        if (scroll_y > max_scroll) scroll_y = max_scroll;

        Vector2 mouse = GetMousePosition();

        bool copy_hover = CheckCollisionPointRec(mouse, copy_btn_rect);
        bool copy_click = copy_hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

        bool wf_hover = wf_has_data && CheckCollisionPointRec(mouse, workflow_btn_rect);
        bool wf_click = wf_hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

        bool close_hover = CheckCollisionPointRec(mouse, close_btn_rect);
        bool close_click = close_hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

        bool top_close_hover = CheckCollisionPointRec(mouse, header_close_rect);
        bool top_close_click = top_close_hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

        /* Set mouse cursor to pointing hand when hovering over any clickable button */
        bool is_hovering_button = copy_hover || wf_hover || close_hover || top_close_hover;
        if (is_hovering_button) {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        } else {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }

        if (copy_click) {
            should_copy_and_exit = true;
            break;
        }
        if (wf_click) {
            should_copy_wf_and_exit = true;
            break;
        }
        if (close_click || top_close_click) {
            break;
        }

        BeginDrawing();
        ClearBackground(bg_dark);

        /* Window Border */
        DrawRectangleLinesEx((Rectangle){ 0, 0, (float)win_w, (float)win_h }, 1, border_color);

        /* 1. Header Bar */
        DrawRectangle(1, 1, win_w - 2, (int)header_height, header_bg);
        DrawLine(0, (int)header_height, win_w, (int)header_height, border_color);

        /* App Icon / Dot & Title */
        DrawCircle((int)(padding + 8.0f), (int)(header_height / 2.0f), 6.0f, accent_indigo);
        DrawTextEx(font, "ComfyPromptExtractor", (Vector2){ padding + 22.0f, (header_height - 18.0f) / 2.0f }, 18.0f, font_spacing, text_bright);

        /* Badges */
        float badge_x = padding + 245.0f;
        if (meta->found && meta->selected_label) {
            char label_str[64];
            snprintf(label_str, sizeof(label_str), "[%s]", meta->selected_label);
            Vector2 label_sz = MeasureTextEx(font, label_str, 13.0f, font_spacing);
            float badge_w = label_sz.x + 16.0f;
            DrawRectangleRounded((Rectangle){ badge_x, (header_height - 24.0f) / 2.0f, badge_w, 24.0f }, 0.4f, 4, badge_bg);
            DrawTextEx(font, label_str, (Vector2){ badge_x + 8.0f, (header_height - label_sz.y) / 2.0f }, 13.0f, font_spacing, badge_green);
            badge_x += badge_w + 8.0f;

            char len_str[64];
            snprintf(len_str, sizeof(len_str), "%zu chars", meta->selected_len);
            Vector2 len_sz = MeasureTextEx(font, len_str, 13.0f, font_spacing);
            float len_w = len_sz.x + 16.0f;
            DrawRectangleRounded((Rectangle){ badge_x, (header_height - 24.0f) / 2.0f, len_w, 24.0f }, 0.4f, 4, badge_bg);
            DrawTextEx(font, len_str, (Vector2){ badge_x + 8.0f, (header_height - len_sz.y) / 2.0f }, 13.0f, font_spacing, text_muted);
            badge_x += len_w + 8.0f;
        }

        /* Filename */
        char file_badge[128];
        snprintf(file_badge, sizeof(file_badge), "• %s", filename);
        DrawTextEx(font, file_badge, (Vector2){ badge_x, (header_height - 13.0f) / 2.0f }, 13.0f, font_spacing, text_muted);

        /* Header Close Icon [X] */
        Color close_ic_col = top_close_hover ? text_bright : text_muted;
        float cx = header_close_rect.x + header_close_rect.width / 2.0f;
        float cy = header_close_rect.y + header_close_rect.height / 2.0f;
        float r = 5.0f;
        DrawLineEx((Vector2){ cx - r, cy - r }, (Vector2){ cx + r, cy + r }, 2.0f, close_ic_col);
        DrawLineEx((Vector2){ cx - r, cy + r }, (Vector2){ cx + r, cy - r }, 2.0f, close_ic_col);

        /* 2. Text Content Box */
        float content_top = header_height + padding;
        Rectangle content_box = { padding, content_top, win_w - (padding * 2.0f), text_panel_height };
        DrawRectangleRounded(content_box, 0.03f, 4, panel_bg);
        DrawRectangleRoundedLinesEx(content_box, 0.03f, 4, 1, border_color);

        /* Scissor area for scrollable text */
        BeginScissorMode((int)(content_box.x + 8.0f), (int)(content_box.y + 8.0f), (int)(content_box.width - 16.0f), (int)(content_box.height - 16.0f));

        float draw_y = content_top + 12.0f - scroll_y;
        for (int i = 0; i < wl.count; i++) {
            if (draw_y + line_height >= content_top && draw_y <= content_top + text_panel_height) {
                DrawTextEx(font, wl.lines[i], (Vector2){ content_box.x + 14.0f, draw_y }, font_size, font_spacing, text_bright);
            }
            draw_y += line_height;
        }

        EndScissorMode();

        /* Scrollbar Indicator */
        if (max_scroll > 0.0f) {
            float scrollbar_height = (text_panel_height / total_content_height) * (text_panel_height - 16.0f);
            if (scrollbar_height < 30.0f) scrollbar_height = 30.0f;
            float scrollbar_y = content_top + 8.0f + (scroll_y / max_scroll) * (text_panel_height - 16.0f - scrollbar_height);
            Rectangle scrollbar_rect = { content_box.x + content_box.width - 8.0f, scrollbar_y, 5.0f, scrollbar_height };
            DrawRectangleRounded(scrollbar_rect, 0.8f, 4, border_color);
        }

        /* 3. Footer / Action Bar */
        DrawRectangle(1, win_h - (int)footer_height, win_w - 2, (int)footer_height, header_bg);
        DrawLine(0, win_h - (int)footer_height, win_w, win_h - (int)footer_height, border_color);

        /* Left Hints */
        DrawTextEx(font, "Enter / Ctrl+C : Copy Prompt | Ctrl+W : Copy Workflow", (Vector2){ padding + 4.0f, win_h - footer_height + 13.0f }, 13.0f, font_spacing, text_bright);
        DrawTextEx(font, "Esc / Click Away : Close", (Vector2){ padding + 4.0f, win_h - footer_height + 33.0f }, 12.0f, font_spacing, text_muted);

        /* Close Button */
        Color c_btn = close_hover ? btn_sec_hover : btn_secondary;
        DrawRectangleRounded(close_btn_rect, 0.25f, 4, c_btn);
        DrawRectangleRoundedLinesEx(close_btn_rect, 0.25f, 4, 1, border_color);
        Vector2 close_sz = MeasureTextEx(font, "Close", 15.0f, font_spacing);
        DrawTextEx(font, "Close", (Vector2){ close_btn_rect.x + (close_btn_rect.width - close_sz.x) / 2.0f, close_btn_rect.y + (close_btn_rect.height - close_sz.y) / 2.0f }, 15.0f, font_spacing, text_bright);

        /* Copy Workflow Button */
        Color wf_bg = wf_has_data ? (wf_hover ? (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? (Color){ 30, 58, 138, 255 } : (Color){ 30, 64, 175, 255 }) : (Color){ 30, 41, 59, 255 }) : (Color){ 24, 28, 36, 255 };
        Color wf_border = wf_has_data ? (wf_hover ? accent_hover : border_color) : border_color;
        Color wf_text_color = wf_has_data ? (wf_hover ? (Color){ 255, 255, 255, 255 } : text_bright) : text_muted;

        DrawRectangleRounded(workflow_btn_rect, 0.25f, 4, wf_bg);
        DrawRectangleRoundedLinesEx(workflow_btn_rect, 0.25f, 4, 1, wf_border);
        Vector2 wf_sz = MeasureTextEx(font, "Copy Workflow", 15.0f, font_spacing);
        DrawTextEx(font, "Copy Workflow", (Vector2){ workflow_btn_rect.x + (workflow_btn_rect.width - wf_sz.x) / 2.0f, workflow_btn_rect.y + (workflow_btn_rect.height - wf_sz.y) / 2.0f }, 15.0f, font_spacing, wf_text_color);

        /* Copy Prompt Button */
        Color cp_btn = copy_hover ? (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? accent_active : accent_hover) : accent_indigo;
        DrawRectangleRounded(copy_btn_rect, 0.25f, 4, cp_btn);
        Vector2 cp_sz = MeasureTextEx(font, "Copy Prompt", 15.0f, font_spacing);
        DrawTextEx(font, "Copy Prompt", (Vector2){ copy_btn_rect.x + (copy_btn_rect.width - cp_sz.x) / 2.0f, copy_btn_rect.y + (copy_btn_rect.height - cp_sz.y) / 2.0f }, 15.0f, font_spacing, (Color){ 255, 255, 255, 255 });

        EndDrawing();
    }

    free_wrapped_lines(&wl);
    if (custom_font_loaded) {
        UnloadFont(font);
    }
    CloseWindow();

    if (should_copy_and_exit && meta->found && meta->selected_val) {
        copy_to_clipboard(meta->selected_val);
    } else if (should_copy_wf_and_exit) {
        const char *wf_str = meta->raw_workflow ? meta->raw_workflow : meta->raw_prompt;
        if (wf_str) {
            copy_to_clipboard(wf_str);
        }
    }
}

/* --- Main Entry Point --- */

static void print_usage(const char *prog_name) {
    fprintf(stderr,
        "ComfyPromptExtractor (CPE) v%s\n"
        "Usage: %s [options] <image.png>\n\n"
        "Options:\n"
        "  -p, --prompt       Extract clean prompt text (default)\n"
        "  -n, --negative     Extract negative prompt text\n"
        "  -r, --raw          Extract raw prompt JSON metadata graph\n"
        "  -w, --workflow     Extract raw workflow JSON metadata\n"
        "      --cli          Force CLI mode (print text directly to stdout)\n"
        "      --gui          Force GUI mode (open compact UI window)\n"
        "  -h, --help         Show this help message\n"
        "  -v, --version      Show version information\n\n"
        "Behavior:\n"
        "  • Terminal (TTY)   : Outputs prompt text directly to stdout and exits 0\n"
        "  • Desktop / GUI    : Opens compact borderless window with Copy button & hotkeys\n",
        CPE_VERSION, prog_name);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *filepath = NULL;
    enum { MODE_AUTO, MODE_FORCE_CLI, MODE_FORCE_GUI } mode = MODE_AUTO;
    OutputTarget target = TARGET_PROMPT_TEXT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("ComfyPromptExtractor v%s\n", CPE_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--cli") == 0) {
            mode = MODE_FORCE_CLI;
        } else if (strcmp(argv[i], "--gui") == 0) {
            mode = MODE_FORCE_GUI;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--prompt") == 0) {
            target = TARGET_PROMPT_TEXT;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--negative") == 0) {
            target = TARGET_NEGATIVE_TEXT;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--raw") == 0) {
            target = TARGET_RAW_PROMPT_JSON;
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--workflow") == 0) {
            target = TARGET_WORKFLOW_JSON;
        } else if (argv[i][0] != '-') {
            filepath = argv[i];
        }
    }

    if (!filepath) {
        fprintf(stderr, "%s: error: no image file specified\n", argv[0]);
        return 1;
    }

    /* Context detection: determine CLI or GUI mode */
    bool is_cli = false;
    if (mode == MODE_FORCE_CLI) {
        is_cli = true;
    } else if (mode == MODE_FORCE_GUI) {
        is_cli = false;
    } else {
        /* Auto: If any standard stream (stdin, stdout, stderr) is a TTY, we were launched
         * from a terminal session -> CLI mode.
         * If none are a TTY (desktop file manager / launcher) -> GUI mode.
         */
        is_cli = (isatty(STDIN_FILENO) != 0 || isatty(STDOUT_FILENO) != 0 || isatty(STDERR_FILENO) != 0);
    }

    /* Parse PNG chunk metadata and extract prompt texts */
    MetadataResult meta;
    PngStatus status = parse_png_metadata(filepath, &meta);

    if (status == PNG_OK) {
        switch (target) {
            case TARGET_PROMPT_TEXT:
                if (meta.prompt_text) {
                    meta.selected_label = "Prompt";
                    meta.selected_val = meta.prompt_text;
                    meta.selected_len = meta.prompt_text_len;
                } else if (meta.raw_prompt) {
                    meta.selected_label = "raw_prompt";
                    meta.selected_val = meta.raw_prompt;
                    meta.selected_len = meta.raw_prompt_len;
                }
                break;
            case TARGET_NEGATIVE_TEXT:
                if (meta.negative_text) {
                    meta.selected_label = "Negative";
                    meta.selected_val = meta.negative_text;
                    meta.selected_len = meta.negative_text_len;
                }
                break;
            case TARGET_RAW_PROMPT_JSON:
                if (meta.raw_prompt) {
                    meta.selected_label = "raw_prompt";
                    meta.selected_val = meta.raw_prompt;
                    meta.selected_len = meta.raw_prompt_len;
                }
                break;
            case TARGET_WORKFLOW_JSON:
                if (meta.raw_workflow) {
                    meta.selected_label = "workflow";
                    meta.selected_val = meta.raw_workflow;
                    meta.selected_len = meta.raw_workflow_len;
                }
                break;
        }
    }

    /* CLI Mode: Zero GUI overhead, sub-millisecond return */
    if (is_cli) {
        if (status == PNG_ERR_IO) {
            fprintf(stderr, "%s: error: cannot open '%s' (file not found or unreadable)\n", argv[0], filepath);
            free_metadata(&meta);
            return 1;
        } else if (status == PNG_ERR_NOT_PNG) {
            fprintf(stderr, "%s: error: '%s' is not a valid PNG file\n", argv[0], filepath);
            free_metadata(&meta);
            return 1;
        } else if (status != PNG_OK || !meta.selected_val || meta.selected_len == 0) {
            fprintf(stderr, "%s: error: no ComfyUI prompt text found in '%s'\n", argv[0], filepath);
            free_metadata(&meta);
            return 1;
        }

        fputs(meta.selected_val, stdout);
        if (meta.selected_len > 0 && meta.selected_val[meta.selected_len - 1] != '\n') {
            putchar('\n');
        }
        fflush(stdout);
        free_metadata(&meta);
        return 0;
    }

    /* GUI Mode: Launch sleek Raylib UI */
    run_gui_mode(filepath, &meta);
    free_metadata(&meta);
    return 0;
}
