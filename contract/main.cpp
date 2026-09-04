// ========================
// File: main.cpp
// ========================
#pragma GCC optimize(3,"Ofast","inline")
#include "Graph.h"
// #include "Contraction.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <string>
#include <set>
#include <fstream>
#include <sys/stat.h>
std::string dataset_name = "unknown";
// 辅助函数：创建目录(支持多级, /tmp 沙盒需要)
bool createDirectory(const std::string& path) {
    #ifdef _WIN32
        return _mkdir(path.c_str()) == 0;
    #else
        std::string cur;
        for (size_t i = 0; i < path.size(); ++i) {
            cur += path[i];
            if (path[i] == '/' || i + 1 == path.size()) {
                if (cur != "/" && cur != ".") mkdir(cur.c_str(), 0755);  // 已存在则忽略
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
        return;
    }

    // 确保输出目录存在
    createDirectory(output_dir);

    // 在输出目录下记录本次收缩的形状顺序, 标记这个文件夹的结果是用哪种顺序产生的
    {
        std::ofstream of(output_dir + "/contraction_order.txt");
        if (of.is_open()) {
            of << "# 本次收缩的形状顺序 (按执行先后)\n";
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

    // threshold kl ku, typeOrder 控制收缩形状种类与顺序
    G.contract(0, 4, ku, typeOrder);
    G.buildSuperGraphAdjacency();   // 独立测超图邻接表构建(不受SKIP_EXPORT影响)


    //新增参数
    G.buildContractedGraphWithoutInternalEdges(output_dir);

    // 导出结果 new  datasetname
#ifndef SKIP_EXPORT
    // SKIP_EXPORT 跳过慢段(超图邻接/超边映射构建, 用于只测 contracted_graph2 构建时加速)
    G.exportContractedGraph(output_dir,dataset_name);
#endif
    std::cout << "Results saved to " << output_dir << std::endl;
}

int main(int argc, char** argv) {
    // 参数验证
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dataset_name> [ku=500] [shapes]\n";
        std::cerr << "  shapes: 逗号分隔的形状顺序, 可选 clique/diamond/star/path\n";
        std::cerr << "          不传则默认 clique,diamond,star,path\n";
        std::cerr << "Example: " << argv[0] << " WikiTalk 500 clique,star,diamond\n";
        std::cerr << "The program will automatically find: /data/tanjinlin/GTdata/test_data/<dataset_name>.txt\n";
        return 1;
    }

    dataset_name = argv[1];
    int ku = (argc >= 3) ? std::stoi(argv[2]) : 500;

    std::vector<std::string> typeOrder = {"clique", "star", "diamond", "path"};
    if (argc >= 4) {
        typeOrder.clear();
        // 按逗号切分 argv[3]
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

    std::string input_path = "/data/tanjinlin/GTdata/test_data/" + dataset_name + ".txt";

    std::string output_dir = "../data/output_" + dataset_name;   // 恢复正式输出 (原计时沙盒 /tmp/contract_timing)

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


