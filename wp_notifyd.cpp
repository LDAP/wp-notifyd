#include <stdio.h>
#include <spdlog/spdlog.h>

#define PROJECT_NAME "wp-notifyd"

int main(int argc, char **argv) {
    if(argc != 1) {
        printf("%s takes no arguments.\n", argv[0]);
        return 1;
    }

    spdlog::set_level(spdlog::level::debug);

    printf("This is project %s.\n", PROJECT_NAME);
    return 0;
}

