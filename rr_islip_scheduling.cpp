#include <iostream>
#include <vector>
#include <deque>
#include <algorithm>
#include <random>
#include <string>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <iomanip>
#include <atomic>
#include <mutex>

struct Packet
{
    // General structure of a packet
    int source;      // Source Port
    int destination; // Destination Port

    // For measuring the KPMs
    std::chrono::steady_clock::time_point arrival_time;
    std::chrono::steady_clock::time_point service_start_time;
    std::chrono::steady_clock::time_point departure_time;

    // Initialising the packet for packet sending
    Packet(int src, int dest) : source(src), destination(dest),
                                arrival_time(std::chrono::steady_clock::now()) {}
};

class Port
{
public:
    int port_id;
    int buffer_size;                        // Queue size
    std::vector<std::deque<Packet>> queues; // Queue initialisation for holding VOQs
    std::vector<int> priority_list;         // Each queue has priority list
    int current_priority_index;             // For noting the current priority element
    int packets_processed;
    int packets_dropped;

    // Initialising the Port Elements
    Port(int id, int size = 64) : port_id(id), buffer_size(size), current_priority_index(0),
                                  packets_processed(0), packets_dropped(0)
    {
        queues.resize(8);
        for (int i = 0; i < 8; ++i)
        {
            priority_list.push_back(i);
        }
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(priority_list.begin(), priority_list.end(), g);
    }
};

class SwitchFabric
{
private:
    int num_ports;                        // No of ports in the router
    std::vector<Port> input_ports;        // Initialsing all input ports
    std::vector<Port> output_ports;       // Initialsing all output ports
    std::unordered_map<int, int> matches; // Since we need to find maximum number of matching in one iteration so for keeping track of it
    std::atomic<bool> running;
    std::mutex fabric_mutex;

    // Performance metrics
    std::chrono::steady_clock::time_point start_time;
    int total_packets_processed;
    int total_packets_dropped;
    double total_turnaround_time;
    double total_waiting_time;

public:
    // Initialising the switching fabric
    SwitchFabric(int ports = 8) : num_ports(ports), total_packets_processed(0),
                                  total_packets_dropped(0), total_turnaround_time(0),
                                  total_waiting_time(0)
    {
        for (int i = 0; i < num_ports; ++i)
        {
            input_ports.emplace_back(i);
            output_ports.emplace_back(i);
        }
    }

    // Since Round Robin and Islip require priority orders of the output and input ports
    // This lists the priority lists for each of the ports
    void print_priority_lists()
    {
        std::cout << "Input Port Priority Lists:\n";
        for (const auto &port : input_ports)
        {
            std::cout << "Port " << port.port_id << ": ";
            for (int p : port.priority_list)
            {
                std::cout << p << " ";
            }
            std::cout << "\n";
        }
        std::cout << "\nOutput Port Priority Lists:\n";
        for (const auto &port : output_ports)
        {
            std::cout << "Port " << port.port_id << ": ";
            for (int p : port.priority_list)
            {
                std::cout << p << " ";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    // To measure the effectiveness of the fabric we had to test it in different environments
    // This is for uniform traffic
    // Here at each interval, we are transferring to each port one packet for each of the output ports
    void generate_uniform_traffic()
    {
        std::lock_guard<std::mutex> lock(fabric_mutex);
        std::cout << "Generating uniform traffic at time: " << std::chrono::system_clock::now().time_since_epoch().count() << ":\n";
        for (auto &input_port : input_ports)
        {
            for (int output_port = 0; output_port < 8; ++output_port)
            {
                if (input_port.queues[output_port].size() < input_port.buffer_size)
                {
                    Packet packet(input_port.port_id, output_port);
                    input_port.queues[output_port].push_back(packet);
                    // std::cout << "  Added: Packet from " << input_port.port_id << " to " << output_port << "\n";
                }
                else
                {
                    std::cout << "  Dropped: Packet from " << input_port.port_id << " to " << output_port << " (Buffer full)\n";
                    input_port.packets_dropped++;
                    total_packets_dropped++;
                }
            }
        }
        std::cout << "\n";
    }

    // This is for non-uniform traffic
    // Here at each interval, packet generation is higher for those with input and output port matching
    void generate_non_uniform_traffic()
    {
        std::lock_guard<std::mutex> lock(fabric_mutex);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);

        std::cout << "Generating non-uniform traffic at time: " << std::chrono::system_clock::now().time_since_epoch().count() << ":\n";
        for (auto &input_port : input_ports)
        {
            for (int output_port = 0; output_port < num_ports; ++output_port)
            {
                // Higher probability for diagonal traffic (input_port == output_port)
                double probability = (input_port.port_id == output_port) ? 0.8 : 0.2;

                if (dis(gen) < probability)
                {
                    if (input_port.queues[output_port].size() < input_port.buffer_size)
                    {
                        Packet packet(input_port.port_id, output_port);
                        input_port.queues[output_port].push_back(packet);
                        // std::cout << "  Added: Packet from " << input_port.port_id << " to " << output_port << "\n";
                    }
                    else
                    {
                        std::cout << "  Dropped: Packet from " << input_port.port_id << " to " << output_port << " (Buffer full)\n";
                        input_port.packets_dropped++;
                        total_packets_dropped++;
                    }
                }
            }
        }
        std::cout << "\n";
    }

    // This is for generating bursty traffic
    // Here at each interval, if it's a bursty port it gets packets between 1-6 and others get only 1
    void generate_bursty_traffic()
    {
        std::lock_guard<std::mutex> lock(fabric_mutex);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> port_dis(0, num_ports - 1);
        std::uniform_int_distribution<> burst_dis(1, 6); // Burst size between 1 and 6

        std::cout << "Generating bursty traffic at time: " << std::chrono::system_clock::now().time_since_epoch().count() << ":\n";

        // Select a random number of ports to receive bursts (between 1 and num_ports/2)
        int burst_port_count = std::uniform_int_distribution<>(1, num_ports / 2)(gen);
        std::vector<int> burst_ports;
        for (int i = 0; i < burst_port_count; ++i)
        {
            burst_ports.push_back(port_dis(gen));
        }

        for (auto &input_port : input_ports)
        {
            int burst_size = (std::find(burst_ports.begin(), burst_ports.end(), input_port.port_id) != burst_ports.end())
                                 ? burst_dis(gen)
                                 : 1; // Burst ports get 1-6 packets, others get 1

            for (int i = 0; i < burst_size; ++i)
            {
                int output_port = port_dis(gen);
                if (input_port.queues[output_port].size() < input_port.buffer_size)
                {
                    Packet packet(input_port.port_id, output_port);
                    input_port.queues[output_port].push_back(packet);
                    // std::cout << "  Added: Packet from " << input_port.port_id << " to " << output_port << "\n";
                }
                else
                {
                    std::cout << "  Dropped: Packet from " << input_port.port_id << " to " << output_port << " (Buffer full)\n";
                    input_port.packets_dropped++;
                    total_packets_dropped++;
                }
            }
        }
        std::cout << "\n";
    }

    // For keeping track of input queues states
    void print_input_state()
    {
        std::lock_guard<std::mutex> lock(fabric_mutex);

        std::cout << "Input Port States:\n";
        for (const auto &port : input_ports)
        {
            std::cout << "Port " << port.port_id << ":\n";
            for (int i = 0; i < 8; ++i)
            {
                std::cout << "  Queue " << i << ": " << port.queues[i].size() << " packets\n";
            }
        }
        std::cout << "\n";
    }

    // Implementation of Round Robin Algorithm
    // First is request phase: All non-empty VOQs send the requests
    // Second is granting phase: All output ports check its priority list and grant to the one pointing by the current_pointer
    // Third is accessing phase: If the VOQ was granted request, the input port consults its priority list to decide to which output port to send.
    // Untill maximal matching is obtained: The loop runs keeping a track of matched ports
    void round_robin_scheduling()
    {
        std::lock_guard<std::mutex> lock(fabric_mutex);
        int iteration = 0;
        bool changes_made;

        do
        {
            iteration++;
            std::cout << "\n==== Scheduling Iteration " << iteration << " ====\n";

            changes_made = false;

            std::vector<std::pair<int, int>> requests;
            std::vector<std::pair<int, int>> grants;
            std::vector<std::pair<int, int>> accepts;

            // Sending of requests
            for (const auto &input_port : input_ports)
            {
                for (int i = 0; i < 8; ++i)
                {
                    if (!input_port.queues[i].empty() && matches.find(input_port.port_id) == matches.end())
                    {
                        requests.emplace_back(input_port.port_id, i);
                    }
                }
            }

            // Print requests
            // std::cout << "Requests:\n";
            // for (const auto &req : requests)
            // {
            //     std::cout << "Input " << req.first << " -> Output " << req.second << "\n";
            // }
            // std::cout << "\n";

            // Granting of requests
            for (auto &output_port : output_ports)
            {
                if (std::find_if(matches.begin(), matches.end(),
                                 [&](const auto &m)
                                 { return m.second == output_port.port_id; }) == matches.end())
                {
                    int initial_priority = output_port.current_priority_index;
                    bool grant_made = false;
                    do
                    {
                        int target_input = output_port.priority_list[output_port.current_priority_index];
                        auto it = std::find_if(requests.begin(), requests.end(),
                                               [&](const auto &req)
                                               {
                                                   return req.first == target_input && req.second == output_port.port_id;
                                               });
                        if (it != requests.end())
                        {
                            grants.emplace_back(it->first, it->second);
                            grant_made = true;
                            break;
                        }
                        output_port.current_priority_index = (output_port.current_priority_index + 1) % num_ports;
                    } while (output_port.current_priority_index != initial_priority);

                    if (grant_made || output_port.current_priority_index == initial_priority)
                    {
                        output_port.current_priority_index = (output_port.current_priority_index + 1) % num_ports;
                    }
                }
            }

            // Print grants
            // std::cout << "Grants:\n";
            // for (const auto &grant : grants)
            // {
            //     std::cout << "Input " << grant.first << " -> Output " << grant.second << "\n";
            // }
            // std::cout << "\n";

            // Accept phase
            for (auto &input_port : input_ports)
            {
                if (matches.find(input_port.port_id) == matches.end())
                {
                    std::vector<int> output_grants;
                    for (const auto &grant : grants)
                    {
                        if (grant.first == input_port.port_id)
                        {
                            output_grants.push_back(grant.second);
                        }
                    }

                    if (!output_grants.empty())
                    {
                        int initial_priority = input_port.current_priority_index;
                        bool accept_made = false;
                        do
                        {
                            int target_output = input_port.priority_list[input_port.current_priority_index];
                            if (std::find(output_grants.begin(), output_grants.end(), target_output) != output_grants.end())
                            {
                                accepts.emplace_back(input_port.port_id, target_output);
                                accept_made = true;
                                break;
                            }
                            input_port.current_priority_index = (input_port.current_priority_index + 1) % num_ports;
                        } while (input_port.current_priority_index != initial_priority);

                        if (accept_made || input_port.current_priority_index == initial_priority)
                        {
                            input_port.current_priority_index = (input_port.current_priority_index + 1) % num_ports;
                        }
                    }
                }
            }

            // Update matches based on accepts
            for (const auto &[input_id, output_id] : accepts)
            {
                if (matches.find(input_id) == matches.end())
                {
                    matches[input_id] = output_id;
                    changes_made = true;
                }
            }

            // Print matches for the current iteration
            // std::cout << "Matches:\n";
            // for (const auto &match : matches)
            // {
            //     std::cout << "Input " << match.first << " -> Output " << match.second << "\n";
            // }
            // std::cout << "\n";

            // Print pointers for debugging
            std::cout << "Input Port Pointers:\n";
            for (const auto &port : input_ports)
            {
                std::cout << "Port " << port.port_id << ": " << port.priority_list[port.current_priority_index] << "\n";
            }
            std::cout << "Output Port Pointers:\n";
            for (const auto &port : output_ports)
            {
                std::cout << "Port " << port.port_id << ": " << port.priority_list[port.current_priority_index] << "\n";
            }
            std::cout << "\n";

        } while (changes_made);
    }

    // Similar to Round Robin implementation wise but iSLIP scheduling algorithm used for determining the ports
    void iSLIP()
    {
        std::lock_guard<std::mutex> lock(fabric_mutex);
        int iteration = 0;
        bool changes_made;

        do
        {
            iteration++;
            std::cout << "\n==== Scheduling Iteration " << iteration << " ====\n";

            changes_made = false;

            std::vector<std::pair<int, int>> requests;
            std::vector<std::pair<int, int>> grants;
            std::vector<std::pair<int, int>> accepts;

            // Sending of requests
            for (const auto &input_port : input_ports)
            {
                for (int i = 0; i < 8; ++i)
                {
                    if (!input_port.queues[i].empty() && matches.find(input_port.port_id) == matches.end())
                    {
                        requests.emplace_back(input_port.port_id, i);
                    }
                }
            }

            // Print requests
            // std::cout << "Requests:\n";
            // for (const auto &req : requests)
            // {
            //     std::cout << "Input " << req.first << " -> Output " << req.second << "\n";
            // }
            // std::cout << "\n";

            // Granting of requests
            for (auto &output_port : output_ports)
            {
                if (std::find_if(matches.begin(), matches.end(),
                                 [&](const auto &m)
                                 { return m.second == output_port.port_id; }) == matches.end())
                {
                    int initial_priority = output_port.current_priority_index;
                    bool grant_made = false;
                    do
                    {
                        int target_input = output_port.priority_list[output_port.current_priority_index];
                        auto it = std::find_if(requests.begin(), requests.end(),
                                               [&](const auto &req)
                                               {
                                                   return req.first == target_input && req.second == output_port.port_id;
                                               });
                        if (it != requests.end())
                        {
                            grants.emplace_back(it->first, it->second);
                            grant_made = true;
                            break;
                        }
                        output_port.current_priority_index = (output_port.current_priority_index + 1) % num_ports;
                    } while (output_port.current_priority_index != initial_priority);

                    // Don't move the pointer here. We'll move it only if the grant is accepted.
                }
            }

            // Print grants
            // std::cout << "Grants:\n";
            // for (const auto &grant : grants)
            // {
            //     std::cout << "Input " << grant.first << " -> Output " << grant.second << "\n";
            // }
            // std::cout << "\n";

            // Accept phase
            for (auto &input_port : input_ports)
            {
                if (matches.find(input_port.port_id) == matches.end())
                {
                    std::vector<int> output_grants;
                    for (const auto &grant : grants)
                    {
                        if (grant.first == input_port.port_id)
                        {
                            output_grants.push_back(grant.second);
                        }
                    }

                    if (!output_grants.empty())
                    {
                        int initial_priority = input_port.current_priority_index;
                        bool accept_made = false;
                        do
                        {
                            int target_output = input_port.priority_list[input_port.current_priority_index];
                            if (std::find(output_grants.begin(), output_grants.end(), target_output) != output_grants.end())
                            {
                                accepts.emplace_back(input_port.port_id, target_output);
                                accept_made = true;
                                break;
                            }
                            input_port.current_priority_index = (input_port.current_priority_index + 1) % num_ports;
                        } while (input_port.current_priority_index != initial_priority);

                        if (accept_made)
                        {
                            input_port.current_priority_index = (input_port.current_priority_index + 1) % num_ports;
                        }
                    }
                }
            }

            // Update matches based on accepts and move output port pointers
            for (const auto &[input_id, output_id] : accepts)
            {
                if (matches.find(input_id) == matches.end())
                {
                    matches[input_id] = output_id;
                    changes_made = true;

                    // Move the output port's pointer only when the grant is accepted
                    output_ports[output_id].current_priority_index = (output_ports[output_id].current_priority_index + 1) % num_ports;
                }
            }

            // Print matches for the current iteration
            // std::cout << "Matches:\n";
            // for (const auto &match : matches)
            // {
            //     std::cout << "Input " << match.first << " -> Output " << match.second << "\n";
            // }
            // std::cout << "\n";

            // Print pointers for debugging
            std::cout << "Input Port Pointers:\n";
            for (const auto &port : input_ports)
            {
                std::cout << "Port " << port.port_id << ": " << port.priority_list[port.current_priority_index] << "\n";
            }
            std::cout << "Output Port Pointers:\n";
            for (const auto &port : output_ports)
            {
                std::cout << "Port " << port.port_id << ": " << port.priority_list[0] << "\n";
            }
            std::cout << "\n";

        } while (changes_made);
    }

    // Transmission of the packet after maximal matching found
    void transfer_data()
    {
        std::lock_guard<std::mutex> lock(fabric_mutex);

        for (const auto &[input_id, output_id] : matches)
        {
            if (!input_ports[input_id].queues[output_id].empty())
            {
                Packet &packet = input_ports[input_id].queues[output_id].front();
                packet.service_start_time = std::chrono::steady_clock::now();
                packet.departure_time = packet.service_start_time;

                double turnaround_time = std::chrono::duration<double>(packet.departure_time - packet.arrival_time).count();
                double waiting_time = std::chrono::duration<double>(packet.service_start_time - packet.arrival_time).count();

                // Ensure waiting time is not negative
                waiting_time = std::max(0.0, waiting_time);

                total_turnaround_time += turnaround_time;
                total_waiting_time += waiting_time;

                input_ports[input_id].queues[output_id].pop_front();
                input_ports[input_id].packets_processed++;
                total_packets_processed++;

                std::cout << "Transferring packet from input port " << input_id
                          << " to output port " << output_id
                          << " (Waiting time: " << waiting_time << " seconds)" << std::endl;
            }
        }
    }

    // Prints the KPM values
    void print_performance_metrics()
    {
        double elapsed_time = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();

        std::cout << "\n==== Performance Metrics ====\n";
        std::cout << "1. Queue Throughput: " << total_packets_processed / elapsed_time << " packets/second\n";
        std::cout << "2. Average Turnaround Time: " << total_turnaround_time / total_packets_processed << " seconds\n";
        std::cout << "3. Average Waiting Time: " << total_waiting_time / total_packets_processed << " seconds\n";
        std::cout << "4. Buffer Occupancy:\n";
        for (const auto &port : input_ports)
        {
            std::cout << "   Input Port " << port.port_id << ": ";
            for (int i = 0; i < 8; ++i)
            {
                std::cout << port.queues[i].size() << " ";
            }
            std::cout << "\n";
        }
        std::cout << "5. Packet Drop Rate: " << (static_cast<double>(total_packets_dropped) / (total_packets_processed + total_packets_dropped)) * 100 << "%\n";
    }

    // Frees the matched port list
    void clear_matches()
    {
        std::lock_guard<std::mutex> lock(fabric_mutex);
        matches.clear();
    }

    // For generating the packets according to test requirements
    void packet_generation_thread(int interval_seconds)
    {
        while (running)
        {
            // generate_uniform_traffic();
            // generate_non_uniform_traffic();
            generate_bursty_traffic();
            std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
        }
    }

    // For simulating the switching fabric, following occurs in sequence:
    // It prints the priority orders for both input and output ports
    // Generates the packets
    // Calls the scheduling algorithm: runs for the aforementioned given time
    // Transfer the data
    // Clear the matches
    // Iterate again
    void simulate(int duration_seconds, int packet_generation_interval)
    {
        running = true;
        start_time = std::chrono::steady_clock::now();
        print_priority_lists();

        std::thread gen_thread(&SwitchFabric::packet_generation_thread, this, packet_generation_interval);

        auto sim_start_time = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - sim_start_time).count() < duration_seconds)
        {
            // std::cout << "\n======== Time: " << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - sim_start_time).count() << " seconds ========\n";

            // print_input_state();

            iSLIP();
            // round_robin_scheduling();

            transfer_data();
            clear_matches();

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        running = false;
        gen_thread.join();

        print_performance_metrics();
    }
};

int main()
{
    SwitchFabric switch_fabric;   // Initialising the switching fabric
    switch_fabric.simulate(3, 1); // Run for 3 seconds, generate packets every 1 seconds
    return 0;
}