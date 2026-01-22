#pragma once
#include <string>

bool TestMySQLConnection(const std::string &host, int port, const std::string &db, const std::string &user, const std::string &pass, bool useSSL, const std::string &caFile, std::string &outError);
