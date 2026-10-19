#pragma once
void socket_census_init();
int socket_census_free();
void socket_census_owner(int fd,const char *host,const char *path);
