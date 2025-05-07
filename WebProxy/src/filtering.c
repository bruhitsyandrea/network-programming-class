#include "proxy.h"
#include <stdlib.h>

char forbidden_sites[100][128]; // Max 100 forbidden sites
int num_forbidden_sites = 0;

void load_forbidden_sites(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open forbidden sites file");
        return;
    }

    num_forbidden_sites = 0; // Clear the current list
    char line[256];

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || strlen(line) <= 1) continue; // Skip comments and empty lines

        line[strcspn(line, "\n")] = '\0'; // Remove newline
        strncpy(forbidden_sites[num_forbidden_sites], line, sizeof(forbidden_sites[num_forbidden_sites]) - 1);
        forbidden_sites[num_forbidden_sites][sizeof(forbidden_sites[num_forbidden_sites]) - 1] = '\0';
        num_forbidden_sites++;
    }

    fclose(file);
    sort_forbidden_sites(); // Add this line to sort after loading
    printf("Forbidden sites list reloaded. Total blocked sites: %d\n", num_forbidden_sites);
}


static int compare_strings(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}


void sort_forbidden_sites() {
    qsort(forbidden_sites, num_forbidden_sites, sizeof(forbidden_sites[0]), compare_strings);
}
int is_site_blocked(const char *host) {
    printf("Checking if site is blocked: %s\n", host);  // DEBUG print

    for (int i = 0; i < num_forbidden_sites; i++) {
        if (strcmp(host, forbidden_sites[i]) == 0) {
            printf("BLOCKED: %s\n", host);
            return 1;
        }

        // Handle 'www.' prefix variations
        if (strncmp(host, "www.", 4) == 0 && strcmp(host + 4, forbidden_sites[i]) == 0) {
            printf("BLOCKED (Removed 'www.'): %s\n", host);
            return 1;
        }
        if (strncmp(forbidden_sites[i], "www.", 4) == 0 && strcmp(forbidden_sites[i] + 4, host) == 0) {
            printf("BLOCKED (Blocked entry had 'www.'): %s\n", host);
            return 1;
        }
    }

    return 0; // Not blocked
}


/*
int is_site_blocked(const char *host) {
    // Check exact match
    if (bsearch(&host, forbidden_sites, num_forbidden_sites, sizeof(forbidden_sites[0]), compare_strings) != NULL) {
        return 1;
    }

    // Check for www. variations
    char modified_host[256];
    if (strncmp(host, "www.", 4) == 0) {
        // Strip 'www.' and check
        strncpy(modified_host, host + 4, sizeof(modified_host) - 1);
        modified_host[sizeof(modified_host) - 1] = '\0';
        if (bsearch(&modified_host, forbidden_sites, num_forbidden_sites, sizeof(forbidden_sites[0]), compare_strings) != NULL) {
            return 1;
        }
    } else {
        // Add 'www.' and check
        snprintf(modified_host, sizeof(modified_host), "www.%s", host);
        if (bsearch(&modified_host, forbidden_sites, num_forbidden_sites, sizeof(forbidden_sites[0]), compare_strings) != NULL) {
            return 1;
        }
    }

    return 0;
}*/

/*
int is_site_blocked(const char *host) {
    printf("Checking if site is blocked: %s\n", host);  // DEBUG print

    for (int i = 0; i < num_forbidden_sites; i++) {
        // Ignore comments in forbidden_sites.txt
        if (forbidden_sites[i][0] == '#') continue;

        char *blocked_site = forbidden_sites[i];

        // Check for an exact match
        if (strcmp(host, blocked_site) == 0) {
            printf("BLOCKED (Exact Match): %s\n", host);
            return 1;
        }

        // Handle missing 'www.' prefix by checking both versions
        if (strncmp(host, "www.", 4) == 0 && strcmp(host + 4, blocked_site) == 0) {
            printf("BLOCKED (Removed 'www.'): %s\n", host);
            return 1;
        }
        if (strncmp(blocked_site, "www.", 4) == 0 && strcmp(blocked_site + 4, host) == 0) {
            printf("BLOCKED (Blocked entry had 'www.'): %s\n", host);
            return 1;
        }
    }

    return 0;
}
*/

void reload_forbidden_sites(int signo) {
    if (signo == SIGINT) {
        printf("\nReloading forbidden sites list...\n");
        load_forbidden_sites("forbidden_sites.txt"); // Re-load the list from file
        printf("Forbidden sites updated.\n");
    }
}