#include "conduit_relay.hpp"
#include "conduit_relay_hdf5.hpp"
#include "conduit_blueprint.hpp"
#include "hdf5.h"
#include <vector>

#include "t_config.hpp"

using namespace conduit;
using namespace conduit::utils;
using namespace conduit::relay;


bool launch_server        = false;
bool use_ssl              = false;
bool use_auth             = false;
bool read_default         = false;
int port                  = 8081;
int sleep_between_updates = 1000;
std::string address       = "127.0.0.1";
std::string datapath      = "";


Node simulate_node;
int simulation_counter = 0;

std::vector<std::string> getFieldTypes(Node & blueprint_node) 
{
    std::vector<std::string> fieldTypes;
    NodeConstIterator itr = blueprint_node["fields"].children();
    while(itr.has_next())
    {
        itr.next();
        fieldTypes.push_back(itr.name());
    }
    return fieldTypes;
}

std::vector<std::string> get_coord_type(Node & blueprint_node) 
{
    std::vector<std::string> pos; 
    if(blueprint_node["coordsets/coords/values"].has_child("x")) 
    {
        pos.push_back(std::string("x"));
        pos.push_back(std::string("y"));
    }
    else 
    {
        pos.push_back(std::string("z"));
        pos.push_back(std::string("r"));
    }
    return pos;
}

void simulate(Node & blueprint_node, std::string & fieldType) 
{
    std::vector<std::string> pos;
    pos = get_coord_type(blueprint_node);

    float64 *new_r_ptr = blueprint_node["coordsets/coords/values"][pos[1]].as_float64_ptr();
    float64 *new_z_ptr = blueprint_node["coordsets/coords/values"][pos[0]].as_float64_ptr();
    index_t r_length = blueprint_node["coordsets/coords/values"][pos[1]].dtype().number_of_elements();
    //shift coordinates of the mesh in some way
    for(index_t i = 0; i < r_length; i++) 
    {
        if(simulation_counter/4 == 0) 
        {
            new_z_ptr[i] = new_z_ptr[i] - 4.0;
        }
        else if(simulation_counter/4 == 1) 
        {
            new_r_ptr[i] = new_r_ptr[i] + 4.0;
        }
        else if(simulation_counter/4 == 2) 
        {
            new_z_ptr[i] = new_z_ptr[i] + 4.0;   
        }
        else if(simulation_counter/4 == 3) 
        {
            new_r_ptr[i] = new_r_ptr[i] - 4.0;   
        }
        else 
        {
            simulation_counter = simulation_counter - 16;
        }
    }

    //change the field value for the center of the mesh
    float64 *new_field_ptr = blueprint_node["fields"][fieldType]["values"].as_float64_ptr();
    index_t field_length = (index_t) blueprint_node["fields"][fieldType]["values"].dtype().number_of_elements();
    for(index_t i = 0; i < field_length; i++) 
    {
        if(i >= field_length*1.0/3 && i <= field_length*2.0/3)
            new_field_ptr[i] = 1.05*new_field_ptr[i];
    }
    simulation_counter++;
}


//in normal update, we will send the entire new connectivity, r, z, or field arrays
Node generate_normal_update_node(Node & new_node) 
{
    Node update_node;
    update_node["normal_update"] = 1;
    
    //add connectivity values
    update_node["conn_value"].set_external(new_node["topologies/mesh/elements/connectivity"]);

    //add 2D-coords values
    std::vector<std::string> pos;
    pos = get_coord_type(new_node);
    update_node["coords"][pos[0]].set_external(new_node["coordsets/coords/values"][pos[0]]);
    update_node["coords"][pos[1]].set_external(new_node["coordsets/coords/values"][pos[1]]);
    
    //add field values
    update_node["fields"].set_external(new_node["fields"]);

    return update_node;
}

/*
This function verifies whether the input node conforms to blueprint format
*/
bool verify_node_format(Node & blueprint_node) 
{
    Node info;

    //make sure the data format is in blueprint mesh
    if(!conduit::blueprint::verify("mesh", blueprint_node, info)) 
    {
        std::cout << "mesh verify failed!" << std::endl;
        info.print();
        return false;
    }
    std::cout <<"Input format is supported. Launch server!!" <<std::endl;
    return true;
}

void generate_node_from_datapath(Node & blueprint_node) 
{
    io::load(datapath, blueprint_node);
}

void generate_node_in_runtime(Node & blueprint_node)
{
    conduit::blueprint::mesh::examples::braid("quads", 200, 200, 1, blueprint_node);
}


void usage()
{
    std::cout << "usage: t_relay_blueprint_websocket"
              << std::endl << std::endl 
              << " arguments:" 
              << std::endl
              << "  launch"
              << std::endl
              << "  ssl"
              << std::endl
              << "  auth"
              << std::endl
              << "  default_data"
              << std::endl
              << " optional arguments:"
              << std::endl
              << "  --address {ip address to bind to (default=127.0.0.1)}" 
              << std::endl
              << "  --port {port number to serve on (default=8081)}" 
              << std::endl
              << "  --datapath {path to the data file}"
              << std::endl
              << "  --sleep_between_updates {sleep time between updates (default = 1000)}"
              << std::endl
              << std::endl << std::endl;

}

void parse_args(int argc, char *argv[])
{
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
        else if(arg_str == "default_data")
        {
            read_default = true;
        }
        else if(arg_str == "--port")
        {
            if(i+1 >= argc)
            {
                CONDUIT_ERROR("expected value following --port option");
            }
            port = atoi(argv[i+1]);
            i++;
        }
        else if(arg_str == "--address")
        {
            if(i+1 >= argc)
            {
                CONDUIT_ERROR("expected value following --address option");
            }
            address = std::string(argv[i+1]);
            i++;  
        }
        else if(arg_str == "--datapath")
        {
            if(i+1 >= argc)
            {
                CONDUIT_ERROR("expected value following --datapath option");
            }
            datapath = std::string(argv[i+1]);
            i++;      
        }
        else if(arg_str == "--sleep_between_updates")
        {
            if(i+1 >= argc)
            {
                CONDUIT_ERROR("expected value following --sleep_between_updates option");
            }
            sleep_between_updates = atoi(argv[i+1]);
            i++;
        }
    }
}

void run()
{
    if(! launch_server)
    {
        std::cout<<"Server not launched. Please use <launch> to launch the server."<<std::endl;
        return;
    }

    Node blueprint_node;
    web::WebServer svr;

    if(read_default)
    {
        generate_node_in_runtime(blueprint_node);
    }
    else if(!datapath.empty()) 
    {
        generate_node_from_datapath(blueprint_node);
    }
    else
    {
        CONDUIT_ERROR("no data read");
    }


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


    svr.set_port(port);
    svr.set_bind_address(address);
    svr.set_document_root(web::web_client_root_directory());
    svr.serve();

    Node new_blueprint_node;
    std::vector<std::string> fieldTypes = getFieldTypes(blueprint_node);
    bool initial_data = true;


    while(svr.is_running()) 
    {
        // send the initial copy
        if(initial_data) 
        {
            // websocket() returns the first active websocket
            svr.websocket()->send(blueprint_node);
            initial_data = false;        
        } 
        else 
        {
            utils::sleep(sleep_between_updates);
            simulate(blueprint_node, fieldTypes[0]);
            svr.websocket()->send(generate_normal_update_node(blueprint_node));            
        }
    }
}

//-----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    try
    {
        int result = 0;
        if (argc == 1)
        {
            usage();
            return -1;
        }
        parse_args(argc, argv);
        run();
    }
    catch(const conduit::Error &e)
    {
        std::cout << "Error launching Conduit Relay Websocket Server:"
                  << std::endl
                  << e.message()
                  << std::endl;
        return -1;
    }

    return 0;
}