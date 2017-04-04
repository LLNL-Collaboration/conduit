#include "conduit_relay.hpp"
#include "conduit_relay_hdf5.hpp"
#include "conduit_blueprint.hpp"
#include "hdf5.h"
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
bool read_hdf5     = false;
bool read_json     = false;

Node simulate(Node & blueprint_node);
Node generate_normal_update_node(Node & new_node);
bool verify_node_format(Node & blueprint_node);
std::vector<std::string> get_coord_type(Node & blueprint_node);
void generate_node_from_json(Node & blueprint_node);
void generate_node_from_hdf5(Node & blueprint_node);
bool verify_node_format(Node & blueprint_node);



Node simulate(Node & blueprint_node) {
    Node new_node;
    //make a hard copy of the original blueprint_node;
    new_node.update(blueprint_node);
    std::vector<std::string> pos;
    pos = get_coord_type(blueprint_node);
    //shift r position by +1
    float64 *new_r_ptr = new_node["coordsets"]["coords"]["values"][pos[1]].as_float64_ptr();
    index_t r_length = new_node["coordsets"]["coords"]["values"][pos[1]].dtype().number_of_elements();
    for(index_t i = 0; i < r_length; i++) {
            new_r_ptr[i] = new_r_ptr[i] + 1.0;
    }

    //change the field value for the center of the mesh
    float64 *new_field_ptr = new_node["fields"]["braid"]["values"].as_float64_ptr();
    index_t field_length = (index_t) new_node["fields"]["braid"]["values"].dtype().number_of_elements();
    for(index_t i = 0; i < field_length; i++) {
        if(i >= field_length*1.0/3 && i <= field_length*2.0/3)
            new_field_ptr[i] = 1.05*new_field_ptr[i];
    }
    return new_node;
}
//in normal update, we will send the entire new connectivity, r, z, or field arrays
Node generate_normal_update_node(Node & new_node) {
    Node update_node;
    update_node["normal_update"] = 1;
    //add connectivity values
    Node temp_conn = new_node["topologies/mesh/elements/connectivity"];
    if(temp_conn.dtype().id() == DataType::INT32_ID) {
        update_node["conn_value"] = temp_conn.as_int_array();    
    } else if (temp_conn.dtype().id() == DataType::INT64_ID) {
        update_node["conn_value"] = temp_conn.as_int64_array();    
    } else {
        std::cout<<"connectivity array data format is not supported."<<std::endl;
        std::cout<<"omit update..."<<std::endl;
        return update_node;
    }
    //add 2D-coords values
    std::vector<std::string> pos;
    pos = get_coord_type(new_node);
    update_node[pos[0]]["value"] = new_node["coordsets/coords/values"][pos[0]].as_double_array();
    update_node[pos[1]]["value"] = new_node["coordsets/coords/values"][pos[1]].as_double_array();
    //add field values
    update_node["field_value"] = new_node["fields"]["braid"]["values"].as_double_array();
    return update_node;
}

/*
This function verifies whether the input node conforms to blueprint format
*/
bool verify_node_format(Node & blueprint_node) {
    Node info;
    // blueprint_node.print();
    //make sure the data format is in blueprint mesh
    if(!conduit::blueprint::verify("mesh", blueprint_node, info)) 
    {
        std::cout << "mesh verify failed!" << std::endl;
        info.print();
        return false;
    }
    //make sure its shape is quad
    if(!(blueprint_node["topologies"]["mesh"]["elements"]["shape"].as_string() == std::string("quad")))
    {
        std::cout << "Not yet supported, blueprint mesh does not have shape quad." << std::endl;
        return false;
    }
    //make sure its field is braid
    if(!(blueprint_node["fields"].has_child("braid")))
    {
        std::cout << "Not yet supported, blueprint mesh does not have field braid." << std::endl;
        return false;   
    }
    if(blueprint_node["coordsets"]["coords"]["values"].number_of_children() != 2) 
    {
        std::cout << "Not yet supported, server only supports 2D rendering." <<std::endl;
        return false;
    }
    std::cout <<"Input format is supported. Launch server!!" <<std::endl;
    return true;
}

std::vector<std::string> get_coord_type(Node & blueprint_node) {
    std::vector<std::string> pos; 
    if(blueprint_node["coordsets"]["coords"]["values"].has_child("x")) {
        pos.push_back(std::string("x"));
        pos.push_back(std::string("y"));
    }
    else {
        pos.push_back(std::string("z"));
        pos.push_back(std::string("r"));
    }
    return pos;
}

void generate_node_from_json(Node & blueprint_node, std::string & wsock_path) {
    /* specify which file you want to read */
    // std::string file_to_use = "testmesh.json";
    // std::string file_to_use = "blueprint_box.json";
    // std::string file_to_use = "blueprint_mesh_fields.json";
    std::string file_to_use = "compressed_blueprint_mesh.json";
    std::string example_blueprint_mesh_path = utils::join_file_path(wsock_path,
                                                         file_to_use);
    std::ifstream blueprint(example_blueprint_mesh_path);
    std::stringstream buffer;
    buffer << blueprint.rdbuf();
    Generator g(buffer.str(), "json", NULL);
    g.walk(blueprint_node);
}

//Needs to implement after building with HDF5 support.
void generate_node_from_hdf5(Node & blueprint_node, std::string & wsock_path)
{
    conduit::blueprint::mesh::examples::braid("quads", 20, 20, 1, blueprint_node);
}

TEST(conduit_relay_web_websocket, websocket_test)
{
    if(! launch_server)
    {
        std::cout<<"Server not launched. Please use <launch> to launch the server."<<std::endl;
        return;
    }

    Node blueprint_node;
    // setup the webserver
    web::WebServer svr;
    std::string wsock_path = utils::join_file_path(web::web_client_root_directory(),
                                                   "blueprint_websocket");


    if(!read_hdf5 && !read_json)
    {
        std::cout<<"No input format specified. Please choose one from <hdf5_test> or <json_test>."<<std::endl;
        return;
    }
    else if (read_hdf5 && read_json)
    {
        std::cout<<"Cannot handle two input formats at once. Please choose one Please choose one from <hdf5_test> or <json_test>."<<std::endl;
        return;
    }
    else if (read_json) 
    {
        generate_node_from_json(blueprint_node, wsock_path);
    }
    else if (read_hdf5)
    {
        generate_node_from_hdf5(blueprint_node, wsock_path);
    }
    // blueprint_node.print();

    if(!verify_node_format(blueprint_node))
    {
        std::cout<<"Input format is not supported."<<std::endl;
        return;
    }    

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
    int initial_data = 0;

    // svr.websocket()->send(blueprint_node);

    while(svr.is_running()) 
    {
        // send the initial copy
        if(!initial_data) {
            // websocket() returns the first active websocket
            svr.websocket()->send(blueprint_node);
            initial_data = 1;        
        }
        utils::sleep(2000);
        //Update blueprint node simulation.
        new_blueprint_node.update(simulate(blueprint_node));
        // svr.websocket()->send(generate_compressed_update_node(blueprint_node, new_blueprint_node));
        svr.websocket()->send(generate_normal_update_node(new_blueprint_node));
        blueprint_node.update(new_blueprint_node);
        
    }
}


//-----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    int result = 0;

    ::testing::InitGoogleTest(&argc, argv);

    for(int i=0; i < argc ; i++)
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
        else if(arg_str == "hdf5_test")
        {
            read_hdf5 = true;
        }
        else if(arg_str == "json_test")
        {
            read_json = true;
        }
    }

    result = RUN_ALL_TESTS();
    return result;
}


//in compressed update, we minimize the data sent
/* =======================This function is no longer maintained =======================*/
// Node generate_compressed_update_node(Node & old_node, Node & new_node) {
//     Node update_node;
//     update_node["compressed_update"] = 1;
//     //compare connectivity 
//     int64 *old_conn_ptr = old_node["topologies"]["mesh"]["elements"]["connectivity"].as_int64_ptr();
//     int64 *new_conn_ptr = new_node["topologies"]["mesh"]["elements"]["connectivity"].as_int64_ptr();
//     index_t conn_length = (index_t) old_node["topologies"]["mesh"]["elements"]["connectivity"].dtype().number_of_elements();
//     std::vector<int64> conn_changed_index;
//     std::vector<int64> conn_changed_value;
//     for(index_t i = 0; i < conn_length; i++) {
//         if(old_conn_ptr[i] != new_conn_ptr[i]) {
//             conn_changed_index.push_back(i);
//             conn_changed_value.push_back(new_conn_ptr[i]);
//         }
//     }
//     if(conn_changed_index.size()) {
//         update_node["conn_index"] = conn_changed_index;
//         update_node["conn_value"] = conn_changed_value;        
//     }
//     //compare r, z

//     std::vector<int64> r_changed_index;
//     std::vector<float64> r_changed_value;
//     std::vector<int64> z_changed_index;
//     std::vector<float64> z_changed_value;
//     float64 *old_r_ptr = old_node["coordsets"]["coords"]["values"]["r"].as_float64_ptr();
//     float64 *new_r_ptr = new_node["coordsets"]["coords"]["values"]["r"].as_float64_ptr();
//     float64 *old_z_ptr = old_node["coordsets"]["coords"]["values"]["z"].as_float64_ptr();
//     float64 *new_z_ptr = new_node["coordsets"]["coords"]["values"]["z"].as_float64_ptr();
//     index_t rz_length = (index_t) old_node["coordsets"]["coords"]["values"]["z"].dtype().number_of_elements();
//     for(index_t i = 0; i < rz_length; i++) {
//         if(old_r_ptr[i] != new_r_ptr[i]) {
//             r_changed_index.push_back(i);
//             r_changed_value.push_back(new_r_ptr[i]);
//         }
//         if(old_z_ptr[i] != new_z_ptr[i]) {
//             z_changed_index.push_back(i);
//             z_changed_value.push_back(new_z_ptr[i]);
//         }
//     }
//     if(r_changed_index.size()) {
//         update_node["r"]["index"] = r_changed_index;
//         update_node["r"]["value"] = r_changed_value;        
//     }
//     if(z_changed_index.size()) {
//         update_node["z"]["index"] = z_changed_index;
//         update_node["z"]["value"] = z_changed_value;        
//     }
//     // update_node.print();
//     // float64 *u_ptr = update_node["r"]["value"].as_float64_ptr();
//     // for (int i = 0; i < 10; ++i)
//     // {
//     //     std::cout<< u_ptr[i]<<std::endl;
//     // }
//     return update_node;
// }

