#include "conduit_relay.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include "gtest/gtest.h"

#include "t_config.hpp"

using namespace conduit;
using namespace conduit::utils;
using namespace conduit::relay;


bool launch_server = false;
bool use_ssl       = false;
bool use_auth      = false;

Node simulate(Node & blueprint_node) {
    Node new_node;
    //make a hard copy of the original blueprint_node;
    new_node.update(blueprint_node);

    //shift r position to the right by 1 unit
    float64 *new_r_ptr = new_node["coordsets"]["coords"]["values"]["r"].as_float64_ptr();
    index_t r_length = new_node["coordsets"]["coords"]["values"]["r"].dtype().number_of_elements();
    for(index_t i = 0; i < r_length; i++) {
        if (new_r_ptr[i] != -1.0)
        {
            new_r_ptr[i] = new_r_ptr[i] + 1.0;
        }
    }
    return new_node;
}

Node generate_update_node(Node & old_node, Node & new_node) {
    Node update_node;
    update_node["update"] = 1;
    //compare connectivity 
    int64 *old_conn_ptr = old_node["topologies"]["mesh"]["elements"]["connectivity"].as_int64_ptr();
    int64 *new_conn_ptr = new_node["topologies"]["mesh"]["elements"]["connectivity"].as_int64_ptr();
    index_t conn_length = (index_t) old_node["topologies"]["mesh"]["elements"]["connectivity"].dtype().number_of_elements();
    std::vector<int64> conn_changed_index;
    std::vector<int64> conn_changed_value;
    for(index_t i = 0; i < conn_length; i++) {
        if(old_conn_ptr[i] != new_conn_ptr[i]) {
            conn_changed_index.push_back(i);
            conn_changed_value.push_back(new_conn_ptr[i]);
        }
    }
    if(conn_changed_index.size()) {
        update_node["conn_index"] = conn_changed_index;
        update_node["conn_value"] = conn_changed_value;        
    }
    //compare r, z

    std::vector<int64> r_changed_index;
    std::vector<float64> r_changed_value;
    std::vector<int64> z_changed_index;
    std::vector<float64> z_changed_value;
    float64 *old_r_ptr = old_node["coordsets"]["coords"]["values"]["r"].as_float64_ptr();
    float64 *new_r_ptr = new_node["coordsets"]["coords"]["values"]["r"].as_float64_ptr();
    float64 *old_z_ptr = old_node["coordsets"]["coords"]["values"]["z"].as_float64_ptr();
    float64 *new_z_ptr = new_node["coordsets"]["coords"]["values"]["z"].as_float64_ptr();
    index_t rz_length = (index_t) old_node["coordsets"]["coords"]["values"]["z"].dtype().number_of_elements();
    for(index_t i = 0; i < rz_length; i++) {
        if(old_r_ptr[i] != new_r_ptr[i]) {
            r_changed_index.push_back(i);
            r_changed_value.push_back(new_r_ptr[i]);
        }
        if(old_z_ptr[i] != new_z_ptr[i]) {
            z_changed_index.push_back(i);
            z_changed_value.push_back(new_z_ptr[i]);
        }
    }
    if(r_changed_index.size()) {
        update_node["r"]["index"] = r_changed_index;
        update_node["r"]["value"] = r_changed_value;        
    }
    if(z_changed_index.size()) {
        update_node["z"]["index"] = z_changed_index;
        update_node["z"]["value"] = z_changed_value;        
    }
    // update_node.print();
    float64 *u_ptr = update_node["r"]["value"].as_float64_ptr();
    // for (int i = 0; i < 10; ++i)
    // {
    //     std::cout<< u_ptr[i]<<std::endl;
    // }
    return update_node;
}

TEST(conduit_relay_web_websocket, websocket_test)
{
    if(! launch_server)
    {
        return;
    }

    std::string wsock_path = utils::join_file_path(web::web_client_root_directory(),
                                                   "blueprint_websocket");

    std::string file_to_use = "testmesh.json";
    // std::string file_to_use = "blueprint_box.json";
    std::string example_blueprint_mesh_path = utils::join_file_path(wsock_path,
                                                         file_to_use);

    std::ifstream blueprint(example_blueprint_mesh_path);
    std::stringstream buffer;
    buffer << blueprint.rdbuf();

    Node blueprint_node;
    Generator g(buffer.str(), "json", NULL);
    g.walk(blueprint_node);
    EXPECT_EQ(blueprint_node.fetch("topologies/mesh/path").to_json(),
                                                     "\"topologies/mesh\"");

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

    Node new_blueprint_node;
    bool initial_data = true;
    while(svr.is_running()) 
    {
        utils::sleep(2000);
        if(initial_data) {
	    // send the initial copy
            // websocket() returns the first active websocket
            svr.websocket()->send(blueprint_node);
            initial_data = false;        
        } else {
	    //Update blueprint node simulation.
	    new_blueprint_node.update(simulate(blueprint_node));
	    svr.websocket()->send(generate_update_node(blueprint_node, new_blueprint_node));
	    blueprint_node.update(new_blueprint_node);
        }
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
        
    }

    result = RUN_ALL_TESTS();
    return result;
}
