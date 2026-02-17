/*
 * gems/meta.c — parse gemspec YAML from a .gem file
 *
 * Extracts metadata.gz from the outer tar (uncompressed), decompresses
 * with zlib, and parses the YAML gemspec using libyaml's document API.
 *
 * The gemspec YAML has Ruby-specific tags (!ruby/object:Gem::Specification)
 * which libyaml handles transparently — we just walk the node tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <third_party/zlib/zlib.h>
#include <yaml.h>

#include "wow/gems/meta.h"
#include "wow/tar.h"

/* Maximum size for metadata.gz (1 MiB — gemspecs are tiny) */
#define MAX_METADATA_SIZE (1024 * 1024)

/* Maximum decompressed YAML size (4 MiB) */
#define MAX_YAML_SIZE (4 * 1024 * 1024)

/* ── Gzip decompression in memory ────────────────────────────────── */

/*
 * Decompress gzip data in memory.
 * Caller must free(*out).  Returns 0 on success.
 */
static int gunzip_mem(const uint8_t *gz, size_t gz_len,
                      uint8_t **out, size_t *out_len)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    /* 16 + MAX_WBITS = gzip header handling */
    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
        fprintf(stderr, "wow: zlib inflateInit2 failed\n");
        return -1;
    }

    if (gz_len > (size_t)UINT_MAX) {
        fprintf(stderr, "wow: compressed metadata too large\n");
        inflateEnd(&strm);
        return -1;
    }

    strm.next_in = (uint8_t *)gz;
    strm.avail_in = (uInt)gz_len;

    size_t alloc = gz_len * 4;
    if (alloc < 4096) alloc = 4096;
    if (alloc > MAX_YAML_SIZE) alloc = MAX_YAML_SIZE;

    uint8_t *buf = malloc(alloc);
    if (!buf) {
        inflateEnd(&strm);
        return -1;
    }

    size_t total = 0;
    int zrc;
    do {
        if (total >= alloc) {
            size_t newalloc = alloc * 2;
            if (newalloc > MAX_YAML_SIZE) {
                fprintf(stderr, "wow: decompressed metadata too large\n");
                free(buf);
                inflateEnd(&strm);
                return -1;
            }
            uint8_t *tmp = realloc(buf, newalloc);
            if (!tmp) {
                free(buf);
                inflateEnd(&strm);
                return -1;
            }
            buf = tmp;
            alloc = newalloc;
        }

        strm.next_out = buf + total;
        strm.avail_out = (uInt)(alloc - total);
        zrc = inflate(&strm, Z_NO_FLUSH);

        if (zrc == Z_STREAM_ERROR || zrc == Z_DATA_ERROR || zrc == Z_MEM_ERROR) {
            fprintf(stderr, "wow: zlib inflate error: %d\n", zrc);
            free(buf);
            inflateEnd(&strm);
            return -1;
        }

        total = alloc - strm.avail_out;
    } while (zrc != Z_STREAM_END);

    inflateEnd(&strm);
    *out = buf;
    *out_len = total;
    return 0;
}

/* ── YAML document tree walking ──────────────────────────────────── */

/*
 * Find a mapping value by key name.
 * Returns the value node, or NULL if not found.
 */
static yaml_node_t *yaml_map_get(yaml_document_t *doc, yaml_node_t *map,
                                 const char *key)
{
    if (!map || map->type != YAML_MAPPING_NODE) return NULL;

    yaml_node_pair_t *pair;
    for (pair = map->data.mapping.pairs.start;
         pair < map->data.mapping.pairs.top; pair++) {
        yaml_node_t *k = yaml_document_get_node(doc, pair->key);
        if (k && k->type == YAML_SCALAR_NODE &&
            strcmp((const char *)k->data.scalar.value, key) == 0) {
            return yaml_document_get_node(doc, pair->value);
        }
    }
    return NULL;
}

/*
 * Get a scalar string from a node (returns pointer to node's data, not a copy).
 */
static const char *yaml_scalar_str(yaml_node_t *node)
{
    if (!node) return NULL;
    if (node->type == YAML_SCALAR_NODE)
        return (const char *)node->data.scalar.value;
    return NULL;
}

/*
 * Extract a version string from a Gem::Version node.
 * These look like:  !ruby/object:Gem::Version { version: "4.1.1" }
 * Or just a plain scalar "4.1.1".
 */
static const char *extract_version(yaml_document_t *doc, yaml_node_t *node)
{
    if (!node) return NULL;

    /* Plain scalar */
    if (node->type == YAML_SCALAR_NODE)
        return yaml_scalar_str(node);

    /* Mapping with "version" key (Gem::Version object) */
    if (node->type == YAML_MAPPING_NODE) {
        yaml_node_t *v = yaml_map_get(doc, node, "version");
        return yaml_scalar_str(v);
    }
    return NULL;
}

/*
 * Collect authors from a sequence node into a comma-separated string.
 */
static char *collect_authors(yaml_document_t *doc, yaml_node_t *seq)
{
    if (!seq || seq->type != YAML_SEQUENCE_NODE) return NULL;

    size_t total = 0;
    yaml_node_item_t *item;

    /* Calculate total length */
    for (item = seq->data.sequence.items.start;
         item < seq->data.sequence.items.top; item++) {
        yaml_node_t *n = yaml_document_get_node(doc, *item);
        const char *s = yaml_scalar_str(n);
        if (s) total += strlen(s) + 2;  /* + ", " */
    }
    if (total == 0) return NULL;

    char *result = malloc(total + 1);
    if (!result) return NULL;
    result[0] = '\0';

    for (item = seq->data.sequence.items.start;
         item < seq->data.sequence.items.top; item++) {
        yaml_node_t *n = yaml_document_get_node(doc, *item);
        const char *s = yaml_scalar_str(n);
        if (!s) continue;
        if (result[0] != '\0') strcat(result, ", ");
        strcat(result, s);
    }
    return result;
}

/*
 * Parse a Gem::Requirement into a constraint string.
 *
 * Structure:
 *   !ruby/object:Gem::Requirement
 *     requirements:
 *     - - "~>"
 *       - !ruby/object:Gem::Version
 *         version: '3.0'
 *     - - ">="
 *       - !ruby/object:Gem::Version
 *         version: '0'
 *
 * Result: "~> 3.0, >= 0"
 */
static char *parse_requirement(yaml_document_t *doc, yaml_node_t *req_node)
{
    if (!req_node) return NULL;

    yaml_node_t *reqs_seq = NULL;

    if (req_node->type == YAML_MAPPING_NODE) {
        reqs_seq = yaml_map_get(doc, req_node, "requirements");
    } else if (req_node->type == YAML_SEQUENCE_NODE) {
        reqs_seq = req_node;
    }

    if (!reqs_seq || reqs_seq->type != YAML_SEQUENCE_NODE) return NULL;

    /* Each item is a 2-element sequence: [operator, version_object] */
    char result[512];
    result[0] = '\0';

    yaml_node_item_t *item;
    for (item = reqs_seq->data.sequence.items.start;
         item < reqs_seq->data.sequence.items.top; item++) {
        yaml_node_t *pair = yaml_document_get_node(doc, *item);
        if (!pair || pair->type != YAML_SEQUENCE_NODE) continue;

        /* Get operator (first element) */
        yaml_node_item_t *elems = pair->data.sequence.items.start;
        int count = (int)(pair->data.sequence.items.top -
                          pair->data.sequence.items.start);
        if (count < 2) continue;

        yaml_node_t *op_node = yaml_document_get_node(doc, elems[0]);
        yaml_node_t *ver_node = yaml_document_get_node(doc, elems[1]);

        const char *op = yaml_scalar_str(op_node);
        const char *ver = extract_version(doc, ver_node);

        if (!op || !ver) continue;

        /* Skip ">= 0" constraints (means "any version") */
        if (strcmp(op, ">=") == 0 && strcmp(ver, "0") == 0)
            continue;

        if (result[0] != '\0') {
            size_t len = strlen(result);
            snprintf(result + len, sizeof(result) - len, ", ");
        }

        size_t len = strlen(result);
        snprintf(result + len, sizeof(result) - len, "%s %s", op, ver);
    }

    return result[0] ? strdup(result) : NULL;
}

/*
 * Parse the dependencies sequence.
 * Each dependency is a Gem::Dependency mapping with:
 *   name: "mustermann"
 *   requirement: !ruby/object:Gem::Requirement { ... }
 *   type: :runtime
 */
static int parse_dependencies(yaml_document_t *doc, yaml_node_t *deps_seq,
                              struct wow_gemspec *spec)
{
    if (!deps_seq || deps_seq->type != YAML_SEQUENCE_NODE) return 0;

    int count = (int)(deps_seq->data.sequence.items.top -
                      deps_seq->data.sequence.items.start);
    if (count <= 0) return 0;

    /* First pass: count runtime deps */
    int n_runtime = 0;
    yaml_node_item_t *item;
    for (item = deps_seq->data.sequence.items.start;
         item < deps_seq->data.sequence.items.top; item++) {
        yaml_node_t *dep = yaml_document_get_node(doc, *item);
        if (!dep || dep->type != YAML_MAPPING_NODE) continue;

        yaml_node_t *type_node = yaml_map_get(doc, dep, "type");
        const char *type = yaml_scalar_str(type_node);
        if (type && strcmp(type, ":runtime") == 0)
            n_runtime++;
    }

    if (n_runtime == 0) return 0;

    spec->deps = calloc((size_t)n_runtime, sizeof(struct wow_gem_dep_info));
    if (!spec->deps) return -1;

    /* Second pass: extract runtime deps */
    size_t idx = 0;
    for (item = deps_seq->data.sequence.items.start;
         item < deps_seq->data.sequence.items.top; item++) {
        yaml_node_t *dep = yaml_document_get_node(doc, *item);
        if (!dep || dep->type != YAML_MAPPING_NODE) continue;

        yaml_node_t *type_node = yaml_map_get(doc, dep, "type");
        const char *type = yaml_scalar_str(type_node);
        if (!type || strcmp(type, ":runtime") != 0) continue;

        yaml_node_t *name_node = yaml_map_get(doc, dep, "name");
        const char *name = yaml_scalar_str(name_node);
        if (!name) continue;

        spec->deps[idx].name = strdup(name);

        yaml_node_t *req_node = yaml_map_get(doc, dep, "requirement");
        spec->deps[idx].constraint = parse_requirement(doc, req_node);

        idx++;
    }
    spec->n_deps = idx;
    return 0;
}

/*
 * Parse a Gem::Requirement into a simple constraint string for
 * required_ruby_version.
 */
static char *parse_simple_requirement(yaml_document_t *doc, yaml_node_t *node)
{
    return parse_requirement(doc, node);
}

/* ── Parse YAML buffer ───────────────────────────────────────────── */

static int parse_gemspec_yaml(const uint8_t *yaml_data, size_t yaml_len,
                              struct wow_gemspec *spec)
{
    yaml_parser_t parser;
    yaml_document_t doc;

    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "wow: yaml_parser_initialize failed\n");
        return -1;
    }

    yaml_parser_set_input_string(&parser, yaml_data, yaml_len);

    if (!yaml_parser_load(&parser, &doc)) {
        fprintf(stderr, "wow: YAML parse error: %s (line %zu)\n",
                parser.problem ? parser.problem : "unknown",
                parser.problem_mark.line + 1);
        yaml_parser_delete(&parser);
        return -1;
    }

    yaml_node_t *root = yaml_document_get_root_node(&doc);
    if (!root || root->type != YAML_MAPPING_NODE) {
        fprintf(stderr, "wow: gemspec YAML root is not a mapping\n");
        yaml_document_delete(&doc);
        yaml_parser_delete(&parser);
        return -1;
    }

    /* Extract fields */
    const char *s;

    s = yaml_scalar_str(yaml_map_get(&doc, root, "name"));
    if (s) spec->name = strdup(s);

    yaml_node_t *ver = yaml_map_get(&doc, root, "version");
    s = extract_version(&doc, ver);
    if (s) spec->version = strdup(s);

    s = yaml_scalar_str(yaml_map_get(&doc, root, "summary"));
    if (s) spec->summary = strdup(s);

    yaml_node_t *authors = yaml_map_get(&doc, root, "authors");
    spec->authors = collect_authors(&doc, authors);

    yaml_node_t *deps = yaml_map_get(&doc, root, "dependencies");
    parse_dependencies(&doc, deps, spec);

    yaml_node_t *ruby_req = yaml_map_get(&doc, root, "required_ruby_version");
    spec->required_ruby_version = parse_simple_requirement(&doc, ruby_req);

    yaml_document_delete(&doc);
    yaml_parser_delete(&parser);
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────── */

int wow_gemspec_parse(const char *gem_path, struct wow_gemspec *spec)
{
    memset(spec, 0, sizeof(*spec));

    /* Extract metadata.gz from the outer tar */
    uint8_t *gz_data = NULL;
    size_t gz_len = 0;
    if (wow_tar_read_entry(gem_path, "metadata.gz",
                           &gz_data, &gz_len, MAX_METADATA_SIZE) != 0) {
        fprintf(stderr, "wow: cannot read metadata.gz from %s\n", gem_path);
        return -1;
    }

    /* Decompress */
    uint8_t *yaml_data = NULL;
    size_t yaml_len = 0;
    int rc = gunzip_mem(gz_data, gz_len, &yaml_data, &yaml_len);
    free(gz_data);

    if (rc != 0) {
        fprintf(stderr, "wow: cannot decompress metadata.gz\n");
        return -1;
    }

    /* Parse YAML */
    rc = parse_gemspec_yaml(yaml_data, yaml_len, spec);
    free(yaml_data);
    return rc;
}

void wow_gemspec_free(struct wow_gemspec *spec)
{
    free(spec->name);
    free(spec->version);
    free(spec->summary);
    free(spec->authors);
    free(spec->required_ruby_version);
    for (size_t i = 0; i < spec->n_deps; i++) {
        free(spec->deps[i].name);
        free(spec->deps[i].constraint);
    }
    free(spec->deps);
    memset(spec, 0, sizeof(*spec));
}
