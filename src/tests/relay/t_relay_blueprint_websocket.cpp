#include "conduit_relay.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include "gtest/gtest.h"

#include "t_config.hpp"

using namespace conduit;
using namespace conduit::utils;
using namespace conduit::relay;


bool launch_server = false;
bool use_ssl       = false;
bool use_auth      = false;

std::string blueprint_path;

TEST(conduit_relay_web_websocket, websocket_test)
{
    if(! launch_server)
    {
        return;
    }

    std::string wsock_path = utils::join_file_path(web::web_client_root_directory(),
                                                   "blueprint_websocket");

    std::string example_blueprint_mesh_path = utils::join_file_path(wsock_path,
                                                         "blueprint_box.json");

    std::ifstream blueprint(example_blueprint_mesh_path);
    std::stringstream buffer;
    buffer << blueprint.rdbuf();

    Node blueprint_node;
    Generator g(buffer.str(), "json", NULL);
    g.walk(blueprint_node);
    EXPECT_EQ(blueprint_node.fetch("topologies/domain0/type").to_json(),
                                                     "\"unstructured\"");
    //blueprint_node.to_json_stream("test.json","json");
    
    /*
    double new_y [] = {-1.0, -1.0, -1.0, -0.5, -0.5, -0.5, 0.0, 0.0, 0.0};
    for (int i = 0; i < 9; i++) {
        Node &list_entry = blueprint_node["coordsets/domain0/values/y"].append();
	list_entry.set(new_y[i]);
    }
				            
    blueprint_node.print();
    */

    // setup the webserver
    web::WebServer svr;

    if(use_ssl)
    {
        std::string cert_file = utils::join_file_path(CONDUIT_T_SRC_DIR,"relay");
        cert_file = utils::join_file_path(cert_file,"t_ssl_cert.pem");
        svr.set_ssl_certificate_file(cert_file);
    }

    if(use_auth)
    {
        std::string auth_file = utils::join_file_path(CONDUIT_T_SRC_DIR,"relay");
        auth_file = utils::join_file_path(auth_file,"t_htpasswd.txt");
        svr.set_htpasswd_auth_domain("test");
        svr.set_htpasswd_auth_file(auth_file);
    }

    svr.set_port(8081);
    svr.set_document_root(wsock_path);

    svr.serve();
    
    double new_y [] = {-1.0, -1.0, -1.0, -0.5, -0.5, -0.5, 0.0, 0.0, 0.0};
    bool hasUpdated = false;
    while(svr.is_running()) 
    {
        utils::sleep(1000);
        
        // websocket() returns the first active websocket
        svr.websocket()->send(blueprint_node);
	
	if (!hasUpdated) {
	    for (int i = 0; i < 9; i++) {
		Node &list_entry = blueprint_node["coordsets/domain0/values/y"].append();
		list_entry.set(new_y[i]);
	    }
	    hasUpdated = true;
	}
	
	//blueprint_node.print();
        // or with a very short timeout
        //svr.websocket(10,100)->send(msg);
        
    }
}


//-----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    int result = 0;

    ::testing::InitGoogleTest(&argc, argv);
    
    for(int i = 0; i < argc; i++)
    {
        std::string arg_str(argv[i]);
        if(arg_str == "launch")
        {
            // actually launch the server
            launch_server = true;
        }
        else if(arg_str == "ssl")
        {
            // test using ssl server cert
            use_ssl = true;
        }
        else if(arg_str == "auth")
        {
            // test using htpasswd auth
            // the user name and password for this example are both "test"
            use_auth = true;
        }
	else if (arg_str == "-p")
	{
	    blueprint_path = std::string(argv[i + 1]);
	}
    }

    result = RUN_ALL_TESTS();
    return result;
}


