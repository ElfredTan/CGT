#pragma GCC optimize(3,"Ofast","inline")
#include "Graph.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <string>
#include <set>
#include <fstream>
#include <sys/stat.h>
std::string dataset_name = "unknown";
// Helper: create a directory (multi-level, needed for /tmp sandboxes)
bool createDirectory(const std::string& path) {
    #ifdef _WIN32
        return _mkdir(path.c_str()) == 0;
    #else
        std::string cur;
        for (size_t i = 0; i < path.size(); ++i) {
            cur += path[i];
            if (path[i] == '/' || i + 1 == path.size()) {
                if (cur != "/" && cur != ".") mkdir(cur.c_str(), 0755);  // ignore if exists
            }
        }
        return true;
    #endif
}

void runBasicTest(const std::string& input_path, const std::string& output_dir, int ku,
                  const std::vector<std::string>& typeOrder) {
    Graph G;

    try {
        G.loadEdgeFormat(input_path);
        std::cout << "Loaded graph with "
                  << G.getNodeCount() << " nodes" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load graph: " << e.what() << std::endl;
        throw;   // let main report the error and exit non-zero
    }

    createDirectory(output_dir);

    // Record the shape order of this contraction in the output dir (marks how this folder was produced)
    {
        std::ofstream of(output_dir + "/contraction_order.txt");
        if (of.is_open()) {
            of << "# Shape order of this contraction (in execution order)\n";
            of << "dataset: " << dataset_name << "\n";
            of << "ku: " << ku << "\n";
            of << "kl: 4\n";
            of << "order:";
            for (size_t i = 0; i < typeOrder.size(); ++i) {
                of << " " << typeOrder[i];
            }
            of << "\n";
            of.close();
        } else {
            std::cerr << "Warning: cannot write contraction_order.txt\n";
        }
    }

    // args: threshold kl ku; typeOrder selects the shapes and their order
    G.contract(0, 4, ku, typeOrder);
    G.buildSuperGraphAdjacency();   // supergraph adjacency build (verification)
    G.buildContractedGraphWithoutInternalEdges(output_dir);

    G.exportContractedGraph(output_dir,dataset_name);
    std::cout << "Results saved to " << output_dir << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dataset_name> [ku=500] [shapes]\n";
        std::cerr << "  shapes: comma-separated shape order, from clique/diamond/star/path\n";
        std::cerr << "          defaults to clique,diamond,star,path when omitted\n";
        std::cerr << "Example: " << argv[0] << " WikiTalk 500 clique,star,diamond\n";
        std::cerr << "The program reads the input from <data_dir>/<dataset_name>.txt,\n"
                  << "  where <data_dir> = $GT_DATA_DIR or (default) ../data/raw/test_data\n";
        return 1;
    }

    dataset_name = argv[1];
    int ku = (argc >= 3) ? std::stoi(argv[2]) : 500;

    std::vector<std::string> typeOrder = {"clique", "star", "diamond", "path"};
    if (argc >= 4) {
        typeOrder.clear();
        // split argv[3] by commas
        std::string shapesArg(argv[3]);
        size_t start = 0;
        while (start <= shapesArg.size()) {
            size_t pos = shapesArg.find(',', start);
            std::string tok = shapesArg.substr(start, (pos == std::string::npos ? shapesArg.size() : pos) - start);
            if (!tok.empty()) typeOrder.push_back(tok);
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
        if (typeOrder.empty()) {
            std::cerr << "Error: shapes argument is empty.\n";
            return 1;
        }

        std::set<std::string> seen;
        for (const std::string& t : typeOrder) {
            if (t != "clique" && t != "diamond" && t != "star" && t != "path") {
                std::cerr << "Error: unknown shape '" << t << "'. Valid: clique/diamond/star/path\n";
                return 1;
            }
            if (!seen.insert(t).second) {
                std::cerr << "Error: duplicate shape '" << t << "' in order. Each shape should appear at most once.\n";
                return 1;
            }
        }
    }

    // Input directory: $GT_DATA_DIR overrides the default relative path
    const char* env_dir = std::getenv("GT_DATA_DIR");
    std::string data_dir = env_dir ? env_dir : "../data/raw/test_data";
    if (!data_dir.empty() && data_dir.back() != '/') data_dir += '/';
    std::string input_path = data_dir + dataset_name + ".txt";

    std::string output_dir = "../data/output_" + dataset_name;

    std::cout << "Dataset name: " << dataset_name << std::endl;
    std::cout << "Input path: " << input_path << std::endl;
    std::cout << "Output directory: " << output_dir << std::endl;
    std::cout << "ku: " << ku << std::endl;
    std::cout << "Shapes: ";
    for (size_t i = 0; i < typeOrder.size(); ++i) {
        if (i) std::cout << ",";
        std::cout << typeOrder[i];
    }
    std::cout << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    try {
        runBasicTest(input_path, output_dir, ku, typeOrder);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

