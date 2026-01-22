#include "MySQLTest.hpp"
#include "db/MySQLBackend.hpp"
#include <string>

bool TestMySQLConnection(const std::string &host, int port, const std::string &db, const std::string &user, const std::string &pass, bool useSSL, const std::string &caFile, std::string &outError){
    LoreBook::DBConnectionInfo ci;
    ci.backend = LoreBook::DBConnectionInfo::Backend::MySQL;
    ci.mysql_host = host;
    ci.mysql_port = port;
    ci.mysql_db = db;
    ci.mysql_user = user;
    ci.mysql_password = pass;
    ci.mysql_use_ssl = useSSL;
    ci.mysql_ca_file = caFile;
    LoreBook::MySQLBackend mb;
    if(mb.open(ci, &outError)){
        return true;
    }
    return false;
}
