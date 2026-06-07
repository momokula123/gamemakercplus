#pragma once
// HTTP API Server - reads NPCs from map->characters
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

struct HttpApiState { SOCKET listenSock; bool running; HANDLE thread; };
static HttpApiState g_httpApi = { INVALID_SOCKET, false, nullptr };

static void sendHttpResponse(SOCKET client, const char* status, const char* contentType = "text/plain", const char* body = "", int bodyLen = -1) {
    if (bodyLen == -1) bodyLen = (int)strlen(body);
    char resp[4096];
    int n = snprintf(resp, sizeof(resp),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nAccess-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n",
        status, contentType, bodyLen);
    send(client, resp, n, 0);
    send(client, body, bodyLen, 0);
}

static void sendHttpFile(SOCKET client, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        sendHttpResponse(client, "404 Not Found", "text/plain", "File not found", -1);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(size);
    if (buf) {
        fread(buf, 1, size, f);
        sendHttpResponse(client, "200 OK", "text/html; charset=utf-8", buf, size);
        free(buf);
    }
    fclose(f);
}

static DWORD WINAPI HttpApiThread(LPVOID) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    g_httpApi.listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_httpApi.listenSock == INVALID_SOCKET) { WSACleanup(); return 1; }
    int opt = 1;
    setsockopt(g_httpApi.listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(18080);
    if (bind(g_httpApi.listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(g_httpApi.listenSock); WSACleanup(); return 1;
    }
    listen(g_httpApi.listenSock, 5);
    u_long nb = 1;
    ioctlsocket(g_httpApi.listenSock, FIONBIO, &nb);

    while (g_httpApi.running) {
        sockaddr_in ca = {}; int cl = sizeof(ca);
        SOCKET cs = accept(g_httpApi.listenSock, (sockaddr*)&ca, &cl);
        if (cs == INVALID_SOCKET) { Sleep(50); continue; }
        char buf[4096] = {}; int r = recv(cs, buf, sizeof(buf) - 1, 0);
        if (r <= 0) { closesocket(cs); continue; }
        buf[r] = 0;

        char cmd[128] = {};
        char* q = strstr(buf, "GET /api?cmd=");
        if (q) {
            q += 13;
            char* e = strchr(q, ' ');
            if (!e) e = strchr(q, '&');
            if (e) { int l = (int)(e - q); if (l > 127) l = 127; strncpy(cmd, q, l); }
            else strncpy(cmd, q, 127);
        }

        if (!cmd[0]) {
            if (strstr(buf, "OPTIONS")) sendHttpResponse(cs, "200 OK", "OK");
            else sendHttpResponse(cs, "200 OK", "Multi-NPC API: /api?cmd=help");
        } else if (g_gameWnd && g_gameWnd->hwnd && IsWindow(g_gameWnd->hwnd)) {
            HWND hw = g_gameWnd->hwnd;

            // Player controls (PostMessage = simulate keyboard)
            if (strcmp(cmd, "up") == 0) { PostMessage(hw, WM_KEYDOWN, VK_UP, 0); sendHttpResponse(cs, "200 OK", "up"); }
            else if (strcmp(cmd, "down") == 0) { PostMessage(hw, WM_KEYDOWN, VK_DOWN, 0); sendHttpResponse(cs, "200 OK", "down"); }
            else if (strcmp(cmd, "left") == 0) { PostMessage(hw, WM_KEYDOWN, VK_LEFT, 0); sendHttpResponse(cs, "200 OK", "left"); }
            else if (strcmp(cmd, "right") == 0) { PostMessage(hw, WM_KEYDOWN, VK_RIGHT, 0); sendHttpResponse(cs, "200 OK", "right"); }
            else if (strcmp(cmd, "stop") == 0) {
                PostMessage(hw, WM_KEYUP, VK_UP, 0); PostMessage(hw, WM_KEYUP, VK_DOWN, 0);
                PostMessage(hw, WM_KEYUP, VK_LEFT, 0); PostMessage(hw, WM_KEYUP, VK_RIGHT, 0);
                sendHttpResponse(cs, "200 OK", "stopped");
            }
            else if (strcmp(cmd, "pos") == 0) {
                char b[128]; snprintf(b, sizeof(b), "pos: %.1f, %.1f", g_gameWnd->playerX, g_gameWnd->playerY);
                sendHttpResponse(cs, "200 OK", b);
            }

            // NPC list - from map->characters (type == npc or enemy, skip player)
            else if (strcmp(cmd, "npc_list") == 0) {
                char lb[4096] = {}; int off = 0;
                for (auto& c : g_gameWnd->map->characters) {
                    if (c.type == L"player") continue;
                    off += snprintf(lb + off, sizeof(lb) - off, "id=%d|%ls|%ls|%.0f,%.0f|hp=%d\n",
                        c.id, c.name.c_str(), c.type.c_str(), c.worldX, c.worldY, c.hp);
                }
                if (off == 0) snprintf(lb, sizeof(lb), "no NPCs");
                sendHttpResponse(cs, "200 OK", lb);
            }

            // NPC select - pick which NPC to control
            else if (strncmp(cmd, "npc_select", 10) == 0) {
                char* eq = strchr(cmd, '=');
                if (eq) {
                    int id = atoi(eq + 1);
                    g_gameWnd->selectedNpcId = id;
                    char resp[64]; snprintf(resp, sizeof(resp), "selected npc %d", id);
                    sendHttpResponse(cs, "200 OK", resp);
                } else {
                    char* ip = strstr(buf, "id=");
                    if (ip) {
                        int id = atoi(ip + 3);
                        g_gameWnd->selectedNpcId = id;
                        char resp[64]; snprintf(resp, sizeof(resp), "selected npc %d", id);
                        sendHttpResponse(cs, "200 OK", resp);
                    } else sendHttpResponse(cs, "400", "missing id");
                }
            }

            // NPC move - set direction flags for selected NPC
            else if (strcmp(cmd, "npc_up") == 0) { g_gameWnd->apiNpcUp = true; sendHttpResponse(cs, "200 OK", "npc up"); }
            else if (strcmp(cmd, "npc_down") == 0) { g_gameWnd->apiNpcDown = true; sendHttpResponse(cs, "200 OK", "npc down"); }
            else if (strcmp(cmd, "npc_left") == 0) { g_gameWnd->apiNpcLeft = true; sendHttpResponse(cs, "200 OK", "npc left"); }
            else if (strcmp(cmd, "npc_right") == 0) { g_gameWnd->apiNpcRight = true; sendHttpResponse(cs, "200 OK", "npc right"); }
            else if (strcmp(cmd, "npc_stop") == 0) {
                g_gameWnd->apiNpcUp = g_gameWnd->apiNpcDown = g_gameWnd->apiNpcLeft = g_gameWnd->apiNpcRight = false;
                sendHttpResponse(cs, "200 OK", "npc stopped");
            }

            // NPC info
            else if (strncmp(cmd, "npc_info", 8) == 0) {
                char* ip = strstr(buf, "id=");
                if (ip) {
                    int id = atoi(ip + 3);
                    for (auto& c : g_gameWnd->map->characters) {
                        if (c.id == id) {
                            char ib[256]; snprintf(ib, sizeof(ib), "id=%d name=%ls type=%ls pos=%.1f,%.1f hp=%d spd=%.1f",
                                c.id, c.name.c_str(), c.type.c_str(), c.worldX, c.worldY, c.hp, c.speed);
                            sendHttpResponse(cs, "200 OK", ib); goto npc_done;
                        }
                    }
                    sendHttpResponse(cs, "200 OK", "not found");
                    npc_done:;
                } else sendHttpResponse(cs, "400", "missing id");
            }

            
            // NPC scan - return 8x8 grid around NPC center
            else if (strcmp(cmd, "npc_scan") == 0) {
                int npcId = g_gameWnd->selectedNpcId;
                char* ip = strstr(buf, "id=");
                if (ip) npcId = atoi(ip + 3);
                if (npcId < 0) { sendHttpResponse(cs, "400", "text/plain", "no NPC selected"); }
                else {
                    Character* npc = g_gameWnd->map->getCharacter(npcId);
                    if (!npc) { sendHttpResponse(cs, "200 OK", "text/plain", "not found"); }
                    else {
                        int cx = (int)(npc->worldX / TileMap::TILE_SIZE);
                        int cy = (int)(npc->worldY / TileMap::TILE_SIZE);
                        int half = 4;
                        int sx = cx - half, sy = cy - half;
                        char resp[8192]; int off = 0;

                        int npcCount = 0;
                        for (auto& c : g_gameWnd->map->characters) {
                            int tx = (int)(c.worldX / TileMap::TILE_SIZE);
                            int ty = (int)(c.worldY / TileMap::TILE_SIZE);
                            if (tx >= sx && tx < sx + 8 && ty >= sy && ty < sy + 8) npcCount++;
                        }

                        off += snprintf(resp + off, sizeof(resp) - off,
                            "center=%d,%d range=%d,%d,%d,%d entities=%d\n",
                            cx, cy, sx, sy, sx + 7, sy + 7, npcCount);

                        // Terrain layer 0
                        for (int r = 0; r < 8; r++) {
                            for (int c = 0; c < 8; c++)
                                off += snprintf(resp + off, sizeof(resp) - off, "%s%d", c ? "," : "",
                                    g_gameWnd->map->getTile(0, sx + c, sy + r));
                            off += snprintf(resp + off, sizeof(resp) - off, "\n");
                        }
                        // Object layer 2
                        for (int r = 0; r < 8; r++) {
                            for (int c = 0; c < 8; c++)
                                off += snprintf(resp + off, sizeof(resp) - off, "%s%d", c ? "," : "",
                                    g_gameWnd->map->getTile(2, sx + c, sy + r));
                            off += snprintf(resp + off, sizeof(resp) - off, "\n");
                        }
                        // Entities
                        for (auto& c : g_gameWnd->map->characters) {
                            int tx = (int)(c.worldX / TileMap::TILE_SIZE);
                            int ty = (int)(c.worldY / TileMap::TILE_SIZE);
                            if (tx >= sx && tx < sx + 8 && ty >= sy && ty < sy + 8)
                                off += snprintf(resp + off, sizeof(resp) - off, "entity=%d|%ls|%ls|%.0f,%.0f|hp=%d\n",
                                    c.id, c.name.c_str(), c.type.c_str(), c.worldX, c.worldY, c.hp);
                        }
                        sendHttpResponse(cs, "200 OK", "text/plain", resp);
                    }
                }
            }
else if (strcmp(cmd, "quit") == 0) { PostMessage(hw, WM_KEYDOWN, VK_ESCAPE, 0); sendHttpResponse(cs, "200 OK", "quitting"); }
            else if (strcmp(cmd, "help") == 0) { sendHttpResponse(cs, "200 OK",
                "Player: up|down|left|right|stop|pos|quit | NPC: npc_list|npc_select|npc_info|npc_scan|npc_up|npc_down|npc_left|npc_right|npc_stop"); }
            else sendHttpResponse(cs, "400", "unknown cmd");
        } else sendHttpResponse(cs, "503", "game not running");
        closesocket(cs);
    }
    closesocket(g_httpApi.listenSock); g_httpApi.listenSock = INVALID_SOCKET; WSACleanup(); return 0;
}

static void HttpApi_Start() {
    if (g_httpApi.running) return;
    g_httpApi.running = true;
    g_httpApi.thread = CreateThread(nullptr, 0, HttpApiThread, nullptr, 0, nullptr);
}
static void HttpApi_Stop() {
    if (!g_httpApi.running) return;
    g_httpApi.running = false;
    if (g_httpApi.thread) { WaitForSingleObject(g_httpApi.thread, 3000); CloseHandle(g_httpApi.thread); g_httpApi.thread = nullptr; }
}

