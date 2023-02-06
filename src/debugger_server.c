#include <arpa/inet.h>
#include "platform.h"

char arch_description_xml[] = {
#ifdef __x86_64__
#include "../build/x86_64_xml.h"
#elif __aarch64__
#include "../build/aarch64_xml.h"
#endif
};

#ifdef __x86_64__
// triple: x86_64-unknown-unknown
#define ARCH_TRIPLE "7838365F36342D756E6B6E6F776E2D756E6B6E6F776E"
#elif __aarch64__
// triple: aarch64-unknown-unknown
#define ARCH_TRIPLE "616172636836342D756E6B6E6F776E2D756E6B6E6F776E"
#endif

#ifdef __linux__
#define PRIx64 "lx"
#elif __APPLE__
#define PRIx64 "llx"
#endif

struct debugger_server {
    struct vcpu** vcpus;
    uint64_t number_of_vcpus;
    uint64_t active_vcpu;
    int server_fd;
    int connection_fd;
    bool send_ack;
};

void send_frame(struct debugger_server* debugger, size_t frame_length, const char frame[frame_length]) {
    printf("SEND: %.*s\n", (int)frame_length, frame);
    uint8_t check_sum = 0;
    char buffer[1024];
    assert(frame_length <= sizeof(buffer) - 6);
    char* ptr = buffer;
    if(debugger->send_ack)
        *ptr++ = '+';
    *ptr++ = '$';
    for(size_t i = 0; i < frame_length; ++i) {
        *ptr++ = frame[i];
        check_sum += frame[i];
    }
    sprintf(ptr, "#%02x", check_sum);
    ptr += 3;
    frame_length = (size_t)(ptr - buffer);
    assert(send(debugger->connection_fd, buffer, frame_length, 0) == (int)frame_length);
}

void handle_frame(struct debugger_server* debugger, size_t frame_length, char frame[frame_length]) {
    printf("RECV: %.*s\n", (int)frame_length, frame);
    if(strncmp(frame, "g", frame_length) == 0) {
        char response[1024];
        for(size_t reg = 0; reg < NUMBER_OF_REGISTERS; ++reg) {
            uint64_t value = get_register_of_vcpu(debugger->vcpus[debugger->active_vcpu], reg);
            for(size_t i = 0; i < 8; ++i)
                sprintf(response + reg * 16 + i * 2, "%02x", (uint8_t)(value >> (i * 8)));
        }
        send_frame(debugger, NUMBER_OF_REGISTERS * 16, response);
        return;
    } else if(frame[0] == 'p' || frame[0] == 'P') {
        bool is_write = (frame[0] == 'P');
        unsigned int register_index;
        frame[frame_length] = 0;
        sscanf(frame + 1, "%x", &register_index);
        if((size_t)register_index < NUMBER_OF_REGISTERS) {
            if(is_write) {
                size_t offset = 1;
                while(offset < frame_length && frame[offset++] != '=');
                uint64_t value = 0;
                unsigned int byte;
                for(size_t i = 0; i < 8; ++i) {
                    sscanf(frame + offset + i * 2, "%02x", &byte);
                    value |= (uint64_t)byte << (i * 8);
                }
                set_register_of_vcpu(debugger->vcpus[debugger->active_vcpu], (uint64_t)register_index, value);
                send_frame(debugger, 2, "OK");
            } else {
                char buffer[17];
                uint64_t value = get_register_of_vcpu(debugger->vcpus[debugger->active_vcpu], (uint64_t)register_index);
                for(size_t i = 0; i < 8; ++i)
                    sprintf(buffer + i * 2, "%02x", (uint8_t)(value >> (i * 8)));
                send_frame(debugger, 16, buffer);
            }
        } else
            send_frame(debugger, 3, "E00");
        return;
    } else if(frame[0] == 'm' || frame[0] == 'M') {
        bool is_write = (frame[0] == 'M');
        uint64_t virtual_address = 0;
        uint64_t length = 0;
        frame[frame_length] = 0;
        sscanf(frame + 1, "%16"PRIx64",%16"PRIx64, &virtual_address, &length);
        uint64_t physical_address = 0;
        void* host_address = NULL;
        if(resolve_address_using_page_table(debugger->vcpus[debugger->active_vcpu]->page_table, is_write, virtual_address, &physical_address) &&
           resolve_address_of_vm(debugger->vcpus[debugger->active_vcpu]->vm, physical_address, &host_address, length)) {
            if(is_write) {
                size_t offset = 1;
                while(offset < frame_length && frame[offset++] != ':');
                unsigned int byte;
                for(size_t i = 0; i < length; ++i) {
                    sscanf(frame + offset + i * 2, "%02x", &byte);
                    ((uint8_t*)host_address)[i] = (uint8_t)byte;
                }
                send_frame(debugger, 2, "OK");
            } else {
                char response[1024];
                for(size_t i = 0; i < length; ++i)
                    sprintf(&response[i * 2], "%02x", ((uint8_t*)host_address)[i]);
                send_frame(debugger, length * 2, response);
            }
        } else
            send_frame(debugger, 3, "E00");
        return;
    } else if(strncmp(frame, "Hg", 2) == 0) {
        unsigned int vcpu_index = 0;
        frame[frame_length] = 0;
        sscanf(frame + 2, "%x", &vcpu_index);
        if(vcpu_index > 0 && (size_t)vcpu_index <= debugger->number_of_vcpus) {
            debugger->active_vcpu = vcpu_index - 1;
            send_frame(debugger, 2, "OK");
            return;
        }
    } else if(strncmp(frame, "qC", frame_length) == 0) {
        char response[128];
        sprintf(response, "QC%" PRIx64, debugger->active_vcpu + 1);
        send_frame(debugger, strlen(response), response);
        return;
    } else if(strncmp(frame, "?", frame_length) == 0) {
        send_frame(debugger, 3, "S02");
        return;
    } else if(strncmp(frame, "c", frame_length) == 0) {
        run_vcpu(debugger->vcpus[debugger->active_vcpu]);
        send_frame(debugger, 3, "S02");
        return;
    } else if(strncmp(frame, "QStartNoAckMode", frame_length) == 0) {
        send_frame(debugger, 2, "OK");
        debugger->send_ack = false;
        return;
    } else if(strncmp(frame, "qHostInfo", frame_length) == 0) {
        const char* response = "triple:" ARCH_TRIPLE ";endian:little;ptrsize:8;";
        send_frame(debugger, strlen(response), response);
        return;
    } else if(strncmp(frame, "qProcessInfo", frame_length) == 0) {
        char response[128];
        sprintf(response, "pid:%x;", getpid());
        send_frame(debugger, strlen(response), response);
        return;
    } else if(strncmp(frame, "qfThreadInfo", frame_length) == 0) {
        send_frame(debugger, 3, "m1");
        return;
    } else if(strncmp(frame, "qsThreadInfo", frame_length) == 0) {
        send_frame(debugger, 1, "l");
        return;
    } else if(strncmp(frame, "qAttached", frame_length) == 0) {
        send_frame(debugger, 1, "1");
        return;
    } else if(strncmp(frame, "D", frame_length) == 0) {
        send_frame(debugger, 2, "OK");
        return;
    } else if(strncmp(frame, "qSupported:", 11) == 0) {
        const char* response = "PacketSize=3e8;qXfer:features:read+";
        send_frame(debugger, strlen(response), response);
        return;
    } else if(strncmp(frame, "qXfer:features:read:target.xml:", 31) == 0) {
        uint64_t offset = 0;
        uint64_t length = 0;
        frame[frame_length] = 0;
        sscanf(frame + 31, "%"PRIx64",%"PRIx64, &offset, &length);
        if(offset >= sizeof(arch_description_xml)) {
            send_frame(debugger, 1, "l");
            return;
        }
        uint64_t max_length = sizeof(arch_description_xml) - offset;
        if(length > max_length)
            length = max_length;
        char response[1000];
        if(length > sizeof(response) - 1)
            length = sizeof(response) - 1;
        response[0] = 'm';
        memcpy(response + 1, arch_description_xml + offset, length);
        send_frame(debugger, length + 1, response);
        return;
    }
    send_frame(debugger, 0, "");
}

struct debugger_server* create_debugger_server(uint64_t number_of_vcpus, struct vcpu* vcpus[number_of_vcpus], uint16_t port, bool localhost_only) {
    struct debugger_server* debugger = malloc(sizeof(struct debugger_server));
    debugger->vcpus = vcpus;
    debugger->number_of_vcpus = number_of_vcpus;
    debugger->active_vcpu = 0;
    debugger->server_fd = socket(AF_INET6, SOCK_STREAM, 0);
    assert(debugger->server_fd >= 0);
    int flags = 0;
    assert(setsockopt(debugger->server_fd, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&flags, sizeof(flags)) == 0);
    flags = 1;
    assert(setsockopt(debugger->server_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&flags, sizeof(flags)) == 0);
    flags = fcntl(debugger->server_fd, F_GETFL);
    assert(flags >= 0);
    flags &= ~O_NONBLOCK;
    assert(fcntl(debugger->server_fd, F_SETFL, flags) == 0);
    struct sockaddr_in6 socket;
    socket.sin6_family = AF_INET6;
    socket.sin6_port = htons(port);
    socket.sin6_scope_id = 0;
    if(localhost_only)
        inet_pton(AF_INET6, "::1", (void*)&socket.sin6_addr.s6_addr);
    else
        socket.sin6_addr = in6addr_any;
    assert(bind(debugger->server_fd, (void*)&socket, sizeof(socket)) == 0);
    assert(listen(debugger->server_fd, 1) == 0);
    debugger->connection_fd = -1;
    return debugger;
}

void destroy_debugger_server(struct debugger_server* debugger) {
    assert(close(debugger->server_fd) >= 0);
    free(debugger);
}

void run_debugger_server(struct debugger_server* debugger) {
    struct sockaddr_in6 connection;
    socklen_t connection_len = sizeof(connection);
    debugger->connection_fd = accept(debugger->server_fd, (void*)&connection, &connection_len);
    debugger->send_ack = true;
    assert(debugger->connection_fd >= 0);
    char buffer[1024];
    size_t buffer_filled = 0;
    while(1) {
        ssize_t received = read(debugger->connection_fd, buffer + buffer_filled, sizeof(buffer) - buffer_filled);
        if(received <= 0)
            break;
        buffer_filled += (size_t)received;
        size_t frame_start = 0;
        while(frame_start < buffer_filled) {
            while(frame_start < buffer_filled && buffer[frame_start++] != '$');
            size_t frame_end = frame_start;
            while(frame_end < buffer_filled && buffer[frame_end] != '#')
                ++frame_end;
            if(frame_start < frame_end) {
                handle_frame(debugger, frame_end - frame_start, buffer + frame_start);
                ++frame_end;
            }
            frame_start = frame_end;
        }
        memcpy(buffer, buffer + frame_start, buffer_filled - frame_start);
        buffer_filled -= frame_start;
    }
    assert(close(debugger->connection_fd) >= 0);
}
