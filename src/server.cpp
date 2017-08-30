// ENCH headers
#include "enchiladas.h"
#include "EnchiladaServer.h"

// PBNJ headers
#include "pbnj.h"
#include "Camera.h"
#include "Configuration.h"
#include "Renderer.h"
#include "TransferFunction.h"
#include "Volume.h"
#include "TimeSeries.h"

#include <dirent.h>
#include <iostream>
#include <cstdlib>
#include <map>
#include <tuple>
#include <csignal>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

using namespace Net;

static volatile int doShutdown = 0;

void sigintHandler(int sig) {
  doShutdown = 1;
}

void waitForShutdown(ench::EnchiladaServer *ench) {
  std::signal(SIGINT, sigintHandler);

  while (!doShutdown) {
    sleep(1);
  }

  ench->shutdown();
}

bool is_number(const std::string &s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

void die(const std::string &message) {
  std::cerr << "Die: " << message;

  if (errno != 0) {
      std::cerr << " (" << strerror(errno) << ")";
  }

  std::cerr << std::endl;

  std::exit(1);
}

bool exists(const std::string &path) {
  struct stat statbuf;
  return (stat(path.c_str(), &statbuf) == 0);
}

int main(int argc, const char **argv)
{
    /*
     * Check the input parameters
     */
    if (argc != 3)
    {
        std::cerr << "Usage: " 
            << argv[0] 
            << " <configuration directory> " 
            << "<port>";

        std::cerr << std::endl;
        return 1;
    }

    // Variables for parsing the directory files
    std::string config_dir = argv[1];
    DIR *directory = opendir(config_dir.c_str());
    struct dirent *dirp;

    /*
     * A volume hash table that keeps PBNJ objects of a dataset
     * in one place
     */
    std::map<std::string, ench::pbnj_container> volume_map;

    // Must call pbnjInit before using it
    pbnj::pbnjInit(&argc, argv);

    while ((dirp = readdir(directory)) != NULL)
    {
        std::string filename(dirp->d_name);
        std::string::size_type index = filename.rfind(".");
        // if the filename doesn't have an extension
        if (index == std::string::npos) 
        {
            continue;
        }
        std::string extension = filename.substr(index);
        if (extension.compare(".json") == 0)
        {
            pbnj::Configuration *config = new pbnj::Configuration(config_dir + "/" + filename);
            pbnj::Camera *camera = new pbnj::Camera(
                    config->imageWidth, 
                    config->imageHeight);

            // Let's keep a renderer per volume to support time series for now
            pbnj::Renderer **renderer; 
            pbnj::CONFSTATE single_multi = config->getConfigState();
            ench::Dataset dataset;

            /*
             * If we have a single volume at hand
             */
            if (single_multi == pbnj::CONFSTATE::SINGLE_NOVAR 
                    || single_multi == pbnj::CONFSTATE::SINGLE_VAR)
            {
                dataset.volume = new pbnj::Volume(
                        config->dataFilename, 
                        config->dataVariable, 
                        config->dataXDim, 
                        config->dataYDim, 
                        config->dataZDim, true);

                dataset.volume->setColorMap(config->colorMap);
                dataset.volume->setOpacityMap(config->opacityMap);
                dataset.volume->attenuateOpacity(config->opacityAttenuation);
                renderer = new pbnj::Renderer*[1];
                renderer[0] = new pbnj::Renderer();
                renderer[0]->setVolume(dataset.volume);
                renderer[0]->setBackgroundColor(config->bgColor);
                renderer[0]->setCamera(camera);
            }
            /*
             * If we have a time series
             */
            else if (single_multi == pbnj::CONFSTATE::MULTI_VAR 
                    || single_multi == pbnj::CONFSTATE::MULTI_NOVAR)
            {
                dataset.timeseries = new pbnj::TimeSeries(
                        config->globbedFilenames, 
                        config->dataVariable, 
                        config->dataXDim,
                        config->dataYDim,
                        config->dataZDim);
                dataset.timeseries->setColorMap(config->colorMap);
                dataset.timeseries->setOpacityMap(config->opacityMap);
                dataset.timeseries->setOpacityAttenuation(config->opacityAttenuation);
                dataset.timeseries->setMemoryMapping(true);
                dataset.timeseries->setMaxMemory(30);

                renderer = new pbnj::Renderer*[dataset.timeseries->getLength()];
                for (int i = 0; i < dataset.timeseries->getLength(); i++)
                {
                    renderer[i] = new pbnj::Renderer();
                    renderer[i]->setVolume(dataset.timeseries->getVolume(i));
                    renderer[i]->setBackgroundColor(config->bgColor);
                    renderer[i]->setCamera(camera);
                }
            }
            else
            {
                std::cerr<<"Cannot open this type of PBNJ file: "<<filename;
                continue;
            }

            camera->setPosition(config->cameraX, config->cameraY, config->cameraZ);
            volume_map[filename.substr(0, index)] = std::make_tuple(config, 
                    dataset, camera, renderer);
        }
    }

    Net::Address addr;

    if (is_number(argv[2])) {
        Net::Port port(9080);
        port = std::stol(argv[2]);
        addr = Net::Address(Net::Ipv4::any(), port);

    } else {
        if (exists(argv[2])) {
            unlink(argv[2]);
        }

        int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd == -1) die("socket");

        struct sockaddr_un my_addr;
        memset(&my_addr, 0, sizeof(my_addr));
        my_addr.sun_family = AF_UNIX;
        strncpy(my_addr.sun_path, argv[2], sizeof(my_addr.sun_path) - 1);

        if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1)
            die("bind");

        addr = Net::Address::fromUnix((struct sockaddr *)&my_addr);
    }

    ench::EnchiladaServer eserver(addr, volume_map);
    eserver.init(1);

    eserver.start();

    std::thread waitForShutdownThread(waitForShutdown, &eserver);
    waitForShutdownThread.join();

    return 0;
}

