#include "EnchiladaServer.h"

#include <iostream>
#include <cstdlib>
#include <string>

#include "http.h"
#include "router.h"
#include "endpoint.h"

#include "pbnj.h"
#include "Camera.h"
#include "Configuration.h"
#include "Renderer.h"
#include "TransferFunction.h"
#include "TimeSeries.h"

namespace ench {

using namespace Net;

EnchiladaServer::EnchiladaServer(Net::Address addr, std::map<std::string, 
        ench::pbnj_container> vm):  
    httpEndpoint(std::make_shared<Net::Http::Endpoint>(addr)), volume_map(vm)
{
}

void EnchiladaServer::init(size_t threads)
{
    auto opts = Net::Http::Endpoint::options()
        .threads(threads)
        .flags(Net::Tcp::Options::InstallSignalHandler);

    this->httpEndpoint->init(opts);

    setupRoutes();
}

void EnchiladaServer::start()
{
    std::cout << "Listening..." << std::endl;
    this->httpEndpoint->setHandler(router.handler());
    this->httpEndpoint->serve();
}

void EnchiladaServer::shutdown()
{
    std::cout<<"Shutting down."<<std::endl;
    this->httpEndpoint->shutdown();
}

void EnchiladaServer::setupRoutes()
{
    using namespace Net::Rest;
    // serving html files
    Routes::Get(router, "/", Routes::bind(&EnchiladaServer::handleRoot, this));
    // serving static js files
    Routes::Get(router, "/js/:filename", 
            Routes::bind(&EnchiladaServer::handleJS, this));
    // serving static css files
    Routes::Get(router, "/css/:filename",
            Routes::bind(&EnchiladaServer::handleCSS, this));
    // serving renders
    Routes::Get(router, "/image/:dataset/:x/:y/:z/:upx/:upy/:upz/:vx/:vy/:vz/:lowquality/:options?",
            Routes::bind(&EnchiladaServer::handleImage, this));
}

void EnchiladaServer::handleRoot(const Rest::Request &request,
        Net::Http::ResponseWriter response)
{
    Http::serveFile(response, "index.html");
}

void EnchiladaServer::handleJS(const Rest::Request &request, 
        Net::Http::ResponseWriter response)
{
    auto filename = request.param(":filename").as<std::string>();
    filename = "js/" + filename;
    Http::serveFile(response, filename.c_str());
}

void EnchiladaServer::handleCSS(const Rest::Request &request, 
        Net::Http::ResponseWriter response)
{
    auto filename = request.param(":filename").as<std::string>();
    filename = "css/" + filename;
    Http::serveFile(response, filename.c_str());
}

void EnchiladaServer::handleImage(const Rest::Request &request,
        Net::Http::ResponseWriter response)
{

    int camera_x = 0;
    int camera_y = 0;
    int camera_z = 0;

    float up_x = 0;
    float up_y = 1;
    float up_z = 0;

    float view_x = 0;
    float view_y = 0;
    float view_z = 1;

    int lowquality = 0;
    std::string dataset = "";

    if (request.hasParam(":dataset"))
    {
        dataset = request.param(":dataset").as<std::string>();

        camera_x = request.param(":x").as<std::int32_t>();
        camera_y = request.param(":y").as<std::int32_t>();
        camera_z = request.param(":z").as<std::int32_t>();

        up_x = request.param(":upx").as<float>();
        up_y = request.param(":upy").as<float>();
        up_z = request.param(":upz").as<float>();

        view_x = request.param(":vx").as<float>();
        view_y = request.param(":vy").as<float>();
        view_z = request.param(":vz").as<float>();

        lowquality = request.param(":lowquality").as<int>();
    }

    // Check if this dataset exists in the loaded datasets
    if (volume_map.count(dataset) == 0)
    {
        response.send(Http::Code::Not_Found, "Image does not exist");
        return;
    }

    pbnj::Configuration *config = std::get<0>(volume_map[dataset]);
    ench::Dataset udataset = std::get<1>(volume_map[dataset]);
    pbnj::Camera *camera = std::get<2>(volume_map[dataset]);
    pbnj::Renderer **renderer = std::get<3>(volume_map[dataset]);
    
    std::vector<unsigned char> png;

    int renderer_index = 0; // Equal to a valid timestep
    bool onlysave = false;
    std::string filename = "";
    std::string save_filename;

    if (request.hasParam(":options"))
    {
        std::string options_line = request.param(":options").as<std::string>();
        std::vector<std::string> options;
        const char *options_chars = options_line.c_str();
        do
        {
            const char *begin = options_chars;
            while(*options_chars != ',' && *options_chars)
                options_chars++;
            options.push_back(std::string(begin, options_chars));

        } while(0 != *options_chars++);

        for (auto it = options.begin(); it != options.end(); it++)
        {
            if (*it == "colormap")
            {
                it++; // Get the value of the colormap
                if (*it == "viridis")
                    udataset.volume->setColorMap(pbnj::viridis);
                else if (*it == "magma")
                    udataset.volume->setColorMap(pbnj::magma);
            }

            if (*it == "hq")
            {
                it++;
                if (*it == "true")
                {
                    std::cout<<"Woah, easter egg, rendering an 8192x8192 with 8 samples. "<<std::endl;
                    renderer[0]->cameraWidth = camera->imageWidth = 8192;
                    renderer[0]->cameraHeight = camera->imageHeight = 8192;
                    renderer[0]->setSamples(8);
                }
            }

            if (*it == "timestep")
            {
                it++;
                int timestep = std::stoi(*it);
                if (timestep >= 0 && timestep < udataset.timeseries->getLength())
                {
                    renderer_index = timestep;
                }
                else
                {
                    std::cerr<<"Invalid timestep: "<<timestep<<std::endl;
                }
            }

            if (*it == "onlysave")
            {
                it++;
                onlysave = true;
                save_filename = *it;
            }

            if (*it == "filename")
            {
                it++;
                filename = *it;
                renderer_index = udataset.timeseries->getVolumeIndex(filename);
                if (renderer_index == -1)
                {
                    response.send(Http::Code::Not_Found, "Image does not exist");
                    return;
                }        
            }

        }
    }

    if (lowquality == 1)
    {
        renderer[renderer_index]->cameraWidth = camera->imageWidth = 64;
        renderer[renderer_index]->cameraHeight = camera->imageHeight = 64;
    }
    else
    {
        renderer[renderer_index]->cameraWidth = camera->imageWidth = std::min(config->imageWidth, lowquality);
        renderer[renderer_index]->cameraHeight = camera->imageHeight = std::min(config->imageHeight, lowquality);
    }


    camera->setPosition(camera_x, camera_y, camera_z);
    camera->setUpVector(up_x, up_y, up_z);
    camera->setView(view_x, view_y, view_z);

    if (onlysave)
    {
        std::cout<<"Saving to "<<save_filename<<std::endl;
        renderer[renderer_index]->renderImage("data/" + save_filename + ".png");
        response.send(Http::Code::Ok, "saved");
    }
    else
    {
        renderer[renderer_index]->renderToPNGObject(png);
        std::string png_data(png.begin(), png.end());
        auto mime = Http::Mime::MediaType::fromString("image/png");
        response.send(Http::Code::Ok, png_data, mime);
    }

}


}
