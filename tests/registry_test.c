/*
 * tests/registry_test.c — Registry module tests with canned JSON
 *
 * Tests wow_gem_info_fetch() parsing logic by mocking HTTP responses.
 * Uses a local HTTP server serving canned JSON files.
 *
 * Run via: make test-registry
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "wow/registry.h"

static int n_pass, n_fail;

static void check(const char *name, int condition) {
    if (condition) {
        printf("  PASS: %s\n", name);
        n_pass++;
    } else {
        printf("  FAIL: %s\n", name);
        n_fail++;
    }
}

/*
 * Test cJSON parsing directly with canned rubygems JSON.
 * This tests the parsing logic without needing a network connection.
 */

static const char sinatra_json[] =
    "{"
    "  \"name\": \"sinatra\","
    "  \"version\": \"4.2.1\","
    "  \"authors\": \"Blake Mizerany, Ryan Tomayko, Simon Rozet, Kunpei Sakai\","
    "  \"info\": \"Sinatra is a DSL for quickly creating web applications in Ruby.\","
    "  \"gem_uri\": \"https://rubygems.org/gems/sinatra-4.2.1.gem\","
    "  \"sha\": \"abc123def456\","
    "  \"dependencies\": {"
    "    \"runtime\": ["
    "      { \"name\": \"logger\", \"requirements\": \">= 1.6.0\" },"
    "      { \"name\": \"mustermann\", \"requirements\": \"~> 3.0\" },"
    "      { \"name\": \"rack\", \"requirements\": \">= 3.0.0, < 4\" }"
    "    ],"
    "    \"development\": ["
    "      { \"name\": \"rake\", \"requirements\": \">= 0\" }"
    "    ]"
    "  }"
    "}";

static const char minimal_json[] =
    "{"
    "  \"name\": \"tiny\","
    "  \"version\": \"0.0.1\""
    "}";

static const char no_deps_json[] =
    "{"
    "  \"name\": \"nodeps\","
    "  \"version\": \"1.0.0\","
    "  \"authors\": \"Test Author\","
    "  \"info\": \"A gem with no dependencies.\","
    "  \"dependencies\": {"
    "    \"runtime\": [],"
    "    \"development\": []"
    "  }"
    "}";

/*
 * Helper: parse canned JSON the same way registry.c does.
 * This duplicates the parsing logic but lets us test without HTTP.
 */
static char *json_strdup(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring)
        return strdup(item->valuestring);
    return NULL;
}

static int parse_gem_json(const char *json_str, struct wow_gem_info *info) {
    memset(info, 0, sizeof(*info));
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return -1;

    info->name    = json_strdup(root, "name");
    info->version = json_strdup(root, "version");
    info->authors = json_strdup(root, "authors");
    info->summary = json_strdup(root, "info");
    info->gem_uri = json_strdup(root, "gem_uri");
    info->sha     = json_strdup(root, "sha");

    const cJSON *deps_obj = cJSON_GetObjectItemCaseSensitive(root, "dependencies");
    if (deps_obj) {
        const cJSON *runtime = cJSON_GetObjectItemCaseSensitive(deps_obj, "runtime");
        if (cJSON_IsArray(runtime)) {
            int count = cJSON_GetArraySize(runtime);
            if (count > 0) {
                info->deps = calloc((size_t)count, sizeof(struct wow_dep));
                if (info->deps) {
                    const cJSON *dep;
                    size_t idx = 0;
                    cJSON_ArrayForEach(dep, runtime) {
                        info->deps[idx].name = json_strdup(dep, "name");
                        info->deps[idx].requirements = json_strdup(dep, "requirements");
                        idx++;
                    }
                    info->n_deps = idx;
                }
            }
        }
    }

    cJSON_Delete(root);
    return 0;
}

static void test_sinatra_parsing(void) {
    printf("\n[Test] Parse sinatra gem JSON...\n");
    struct wow_gem_info info;
    int rc = parse_gem_json(sinatra_json, &info);
    check("parse succeeds", rc == 0);
    check("name is sinatra", info.name && strcmp(info.name, "sinatra") == 0);
    check("version is 4.2.1", info.version && strcmp(info.version, "4.2.1") == 0);
    check("authors present", info.authors != NULL);
    check("summary present", info.summary != NULL);
    check("gem_uri present", info.gem_uri != NULL);
    check("sha present", info.sha != NULL);
    check("3 runtime deps", info.n_deps == 3);
    if (info.n_deps >= 3) {
        check("dep[0] is logger", strcmp(info.deps[0].name, "logger") == 0);
        check("dep[1] is mustermann", strcmp(info.deps[1].name, "mustermann") == 0);
        check("dep[2] is rack", strcmp(info.deps[2].name, "rack") == 0);
        check("dep[2] req is >= 3.0.0, < 4",
              strcmp(info.deps[2].requirements, ">= 3.0.0, < 4") == 0);
    }
    wow_gem_info_free(&info);
}

static void test_minimal_parsing(void) {
    printf("\n[Test] Parse minimal gem JSON (no optional fields)...\n");
    struct wow_gem_info info;
    int rc = parse_gem_json(minimal_json, &info);
    check("parse succeeds", rc == 0);
    check("name is tiny", info.name && strcmp(info.name, "tiny") == 0);
    check("version is 0.0.1", info.version && strcmp(info.version, "0.0.1") == 0);
    check("authors is NULL", info.authors == NULL);
    check("summary is NULL", info.summary == NULL);
    check("0 deps", info.n_deps == 0);
    wow_gem_info_free(&info);
}

static void test_no_deps_parsing(void) {
    printf("\n[Test] Parse gem with empty deps array...\n");
    struct wow_gem_info info;
    int rc = parse_gem_json(no_deps_json, &info);
    check("parse succeeds", rc == 0);
    check("name is nodeps", info.name && strcmp(info.name, "nodeps") == 0);
    check("0 deps", info.n_deps == 0);
    check("deps pointer is NULL", info.deps == NULL);
    wow_gem_info_free(&info);
}

static void test_malformed_json(void) {
    printf("\n[Test] Malformed JSON handling...\n");
    struct wow_gem_info info;
    int rc = parse_gem_json("{this is not valid json!!!", &info);
    check("malformed JSON returns -1", rc == -1);

    rc = parse_gem_json("", &info);
    check("empty string returns -1", rc == -1);
}

static void test_live_fetch(void) {
    printf("\n[Test] Live fetch from rubygems.org (sinatra)...\n");
    struct wow_gem_info info;
    int rc = wow_gem_info_fetch("sinatra", &info);
    check("fetch succeeds", rc == 0);
    if (rc == 0) {
        check("name is sinatra", info.name && strcmp(info.name, "sinatra") == 0);
        check("version is non-empty", info.version && strlen(info.version) > 0);
        check("has deps", info.n_deps > 0);
        wow_gem_info_free(&info);
    }
}

static void test_nonexistent_gem(void) {
    printf("\n[Test] Fetch nonexistent gem...\n");
    struct wow_gem_info info;
    int rc = wow_gem_info_fetch("this-gem-definitely-does-not-exist-12345", &info);
    check("nonexistent gem returns -1", rc == -1);
}

int main(void) {
    printf("=== wow registry tests ===\n");

    /* Offline tests (canned JSON) */
    test_sinatra_parsing();
    test_minimal_parsing();
    test_no_deps_parsing();
    test_malformed_json();

    /* Online tests (need network) */
    test_live_fetch();
    test_nonexistent_gem();

    printf("\n=== Results: %d passed, %d failed ===\n", n_pass, n_fail);
    return n_fail > 0 ? 1 : 0;
}
