#ifndef ENCH_SERVER_H
#define ENCH_SERVER_H

#include <enchiladas.h>

#include "pbnj.h"
#include "Renderer.h"
#include "Configuration.h"
#include "Camera.h"

#include "http.h"
#include "router.h"
#include "endpoint.h"

namespace ench {

    using namespace Net;

    class EnchiladaServer {

        public:
            EnchiladaServer(Net::Address addr, std::map<std::string, 
                    std::tuple<pbnj::Configuration*, pbnj::Volume*, 
                    pbnj::Camera*, pbnj::Renderer*>> vm);

            void init(size_t threads=2);
            void start();
            void shutdown();

        private:

            void setupRoutes();
            void handleRoot(const Rest::Request &request,
                    Net::Http::ResponseWriter response);
            void handleJS(const Rest::Request &request, 
                    Net::Http::ResponseWriter response);
            void handleCSS(const Rest::Request &request, 
                    Net::Http::ResponseWriter response);
            void handleImage(const Rest::Request &request,
                    Net::Http::ResponseWriter response);

            std::shared_ptr<Net::Http::Endpoint> httpEndpoint;
            Rest::Router router;

            std::map<std::string, std::tuple<pbnj::Configuration*, 
                pbnj::Volume*, pbnj::Camera*, pbnj::Renderer*>> volume_map;
    };
}

#endif
