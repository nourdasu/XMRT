#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <time.h>
#include <math.h>

#define URL "https://tradeogre.com/api/v1/markets"
#define PAIR "XMR-USDT"
#define INTERVAL 60  // seconds
#define ALERT_THRESHOLD 0.10  // 10%

struct MemoryStruct {
    char *memory;
    size_t size;
};

// Callback for libcurl to write data into memory
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

// Extracts the price from the JSON manually
double extract_price(const char *json, const char *pair) {
    char *pos = strstr(json, pair);
    if (!pos) return -1;

    pos = strstr(pos, "\"price\":\"");
    if (!pos) return -1;

    pos += strlen("\"price\":\"");
    return atof(pos);
}

// Uses curl to fetch the price data from TradeOgre
double get_price() {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk = { malloc(1), 0 };

    curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "xmr-tracker/1.0");

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        free(chunk.memory);
        curl_easy_cleanup(curl);
        return -1;
    }

    double price = extract_price(chunk.memory, PAIR);
    free(chunk.memory);
    curl_easy_cleanup(curl);
    return price;
}

// Get current formatted timestamp string
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", t);
}

int main() {
    double last_price = get_price();
    if (last_price < 0) {
        fprintf(stderr, "Failed to get initial price.\n");
        return 1;
    }

    printf("Initial %s price: %.6f\n", PAIR, last_price);

    while (1) {
        sleep(INTERVAL);
        double current_price = get_price();
        if (current_price < 0) {
            printf("Error fetching price.\n");
            continue;
        }

        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));

        // Log to file
        FILE *log = fopen("xmr_log.txt", "a");
        if (log) {
            fprintf(log, "[%s] Price: %.6f\n", timestamp, current_price);
            fclose(log);
        }

        // Calculate % change
        double change = (current_price - last_price) / last_price;
        double pct = change * 100;

        if (change > 0.0001)
            printf("[%s] ↑ Price up: %.6f (+%.2f%%)\n", timestamp, current_price, pct);
        else if (change < -0.0001)
            printf("[%s] ↓ Price down: %.6f (%.2f%%)\n", timestamp, current_price, pct);
        else
            printf("[%s] = Price unchanged: %.6f\n", timestamp, current_price);

        // Alert if 10% movement
        if (fabs(change) >= ALERT_THRESHOLD) {
            char command[256];
            snprintf(command, sizeof(command),
                "notify-send '🚨 XMR ALERT' 'Price moved %.2f%% (%.6f → %.6f)'",
                pct, last_price, current_price);
            system(command);
        }

        last_price = current_price;
    }

    return 0;
}

