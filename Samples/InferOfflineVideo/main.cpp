/**
 * ============================================================================
 *
 * Copyright (C) 2019, Huawei Technologies Co., Ltd. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1 Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *   2 Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *   3 Neither the names of the copyright holders nor the names of the
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * ============================================================================
 */
#include "DataRecv.h"
#include "hiaiengine/api.h"
#include <atomic>
#include <iostream>
#include <libgen.h>
#include <unistd.h>
#include <thread>

static const std::string GRAPH_FILENAME[] = { "./graph0.config", "./graph1.config", "./graph2.config", "./graph3.config"};
// graph id
static const uint32_t GRAPH_ID[] = { 100, 101, 102, 103 };
// destination engine id
static const uint32_t SRC_ENGINE = 101;
// leaf node engine id
static const uint32_t terminators[] = { 106 };
// flag to guard eos signal
static std::atomic<int> g_flag = { 1 };

HIAI_StatusT CustomDataRecvInterface::RecvData(const std::shared_ptr<void>& message)
{
    std::shared_ptr<std::string> data = std::static_pointer_cast<std::string>(message);
    std::cout << "RecvData g_flag " << g_flag << "\n";
    g_flag--;
    return HIAI_OK;
}

// Init and create graph
HIAI_StatusT HIAI_InitAndStartGraph(const std::string& configFile, const uint32_t deviceID)
{
    // Step1: Global System Initialization before using HIAI Engine
    HIAI_StatusT status = HIAI_Init(deviceID);

    if (status != HIAI_OK) {
            printf("Failed to Init device %u\n", deviceID);
    }
    // Step2: Create and Start the Graph
    status = hiai::Graph::CreateGraph(configFile);
    if (status != HIAI_OK) {
        HIAI_ENGINE_LOG(status, "Fail to start graph");
        printf("Failed to start graph %s on device %u\n", configFile.c_str(), deviceID);
        return status;
    }

    // Step3: Set DataRecv Functor
    std::shared_ptr<hiai::Graph> graph = hiai::Graph::GetInstance(GRAPH_ID[deviceID]);
    if (graph == nullptr) {
        HIAI_ENGINE_LOG("Fail to get the graph-%u", GRAPH_ID[deviceID]);
        return status;
    }

    for (int i = 0; i < sizeof(terminators) / sizeof(uint32_t); i++) {
        hiai::EnginePortID target_port_config;
        target_port_config.graph_id = GRAPH_ID[deviceID];
        target_port_config.engine_id = terminators[i];
        target_port_config.port_id = 0;
        graph->SetDataRecvFunctor(target_port_config,
            std::make_shared<CustomDataRecvInterface>(""));
    }
    return HIAI_OK;
}

void my_handler(int s)
{
    printf("Caught signal %d\n", s);
    if (s == 2) {
        for(uint32_t n = 0; n < 4; n++) {
            printf("DestroyGraph %u\n", GRAPH_ID[n]);
            hiai::Graph::DestroyGraph(GRAPH_ID[n]);
        }
        exit(0);
    }
}

int main(int argc, char* argv[])
{
    // cd to directory of main
    char* dirc = strdup(argv[0]);
    if (dirc != NULL) {
        char* dname = ::dirname(dirc);
        int r = chdir(dname);
        if (r != 0) {
            printf("chdir error code %d\n", r);
            return -1;
        }
        free(dirc);
    }

    // init Graph
    uint32_t threads_n = 4;
    std::thread threads[threads_n];
    for (uint32_t n = 0; n < threads_n; n++) {
        HIAI_StatusT ret;
        threads[n] = std::thread([&ret, n](){
            printf("%s %u \n", GRAPH_FILENAME[n].c_str(), n);
            ret = HIAI_InitAndStartGraph(GRAPH_FILENAME[n], n);
            if (ret != HIAI_OK) {
                printf("Fail to start graph %u\n", n);
                return -1;
            }
    
            // send data
            std::shared_ptr<hiai::Graph> graph = hiai::Graph::GetInstance(GRAPH_ID[n]);
            if (nullptr == graph) {
                printf("Fail to get the graph-%u\n", GRAPH_ID[n]);
                return -1;
            }
            hiai::EnginePortID engine_id;
            engine_id.graph_id = GRAPH_ID[n];
            engine_id.engine_id = SRC_ENGINE;
            engine_id.port_id = 0;
            std::shared_ptr<std::string> src_data(new std::string());

            graph->SendData(engine_id, "string", std::static_pointer_cast<void>(src_data));

            // wait for ctrl+c
            struct sigaction sigIntHandler;
            sigIntHandler.sa_handler = my_handler;
            sigemptyset(&sigIntHandler.sa_mask);
            sigIntHandler.sa_flags = 0;
            sigaction(SIGINT, &sigIntHandler, NULL);

            while (g_flag > 0) {
                usleep(10000);
            }

            // end
            hiai::Graph::DestroyGraph(GRAPH_ID[n]);
            printf("[main] destroy graph-%u done\n", GRAPH_ID[n]);

        });
    }

    for (uint32_t n = 0; n < threads_n; n++) threads[n].join();

    return 0;
}
