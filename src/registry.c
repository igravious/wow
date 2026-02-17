#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "wow/http.h"
#include "wow/registry.h"

#define RUBYGEMS_API "https://rubygems.org/api/v1/gems/"

static char *json_strdup(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring)
        return strdup(item->valuestring);
    return NULL;
}

int wow_gem_info_fetch(const char *name, struct wow_gem_info *info) {
    memset(info, 0, sizeof(*info));

    /* Build URL: https://rubygems.org/api/v1/gems/{name}.json */
    size_t urllen = strlen(RUBYGEMS_API) + strlen(name) + 6; /* .json\0 */
    char *url = malloc(urllen);
    if (!url) return -1;
    snprintf(url, urllen, "%s%s.json", RUBYGEMS_API, name);

    struct wow_response resp;
    int rc = wow_http_get(url, &resp);
    free(url);
    if (rc != 0) return -1;

    if (resp.status == 404) {
        fprintf(stderr, "wow: gem '%s' not found on rubygems.org\n", name);
        wow_response_free(&resp);
        return -1;
    }
    if (resp.status != 200) {
        fprintf(stderr, "wow: rubygems.org returned status %d for '%s'\n",
                resp.status, name);
        wow_response_free(&resp);
        return -1;
    }

    /* Null-terminate body for cJSON */
    char *json_str = malloc(resp.body_len + 1);
    if (!json_str) {
        wow_response_free(&resp);
        return -1;
    }
    memcpy(json_str, resp.body, resp.body_len);
    json_str[resp.body_len] = '\0';
    wow_response_free(&resp);

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        fprintf(stderr, "wow: failed to parse JSON for gem '%s'\n", name);
        return -1;
    }

    info->name    = json_strdup(root, "name");
    info->version = json_strdup(root, "version");
    info->authors = json_strdup(root, "authors");
    info->summary = json_strdup(root, "info");
    info->gem_uri = json_strdup(root, "gem_uri");
    info->sha     = json_strdup(root, "sha");

    /* Parse runtime dependencies */
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

void wow_gem_info_free(struct wow_gem_info *info) {
    free(info->name);
    free(info->version);
    free(info->authors);
    free(info->summary);
    free(info->gem_uri);
    free(info->sha);
    for (size_t i = 0; i < info->n_deps; i++) {
        free(info->deps[i].name);
        free(info->deps[i].requirements);
    }
    free(info->deps);
    memset(info, 0, sizeof(*info));
}
