#ifndef CWDEBUG_SERVER_H
#define CWDEBUG_SERVER_H
//
// server.h - part of CWDebug, a source-level debugger for the AmigaOS
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


//
// type declarations
//
typedef struct HostConnection HostConnection;
typedef struct ProtoMessage ProtoMessage;


//
// exported functions
//
HostConnection *create_host_conn();
void destroy_host_conn(HostConnection *p_conn);
void process_remote_commands();

#endif  // CWDEBUG_SERVER_H
