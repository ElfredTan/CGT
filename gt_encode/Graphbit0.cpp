// GraphTwin index
#include<bits/stdc++.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

using namespace std;

const int Gbit = 64;
// 32,64,128

int nodenum, edgenum, max_degree = 0;
vector<vector<int> > Graph;
vector<int> visited;
vector<bitset<Gbit> > GraphBit;
vector<int> Verts, bin, pos;
vector<int> cnt;


void read_graph(char* GraphFile) {
        ifstream input(GraphFile);
        input >> nodenum >> edgenum;
        for (int i = 0; i < nodenum; i++) {
                Graph.push_back(vector<int> (0));
                visited.push_back(-1);
                cnt.push_back(0);
                GraphBit.push_back(bitset<Gbit> (0));
                Verts.push_back(i);
        }
        pos.resize(nodenum);
        bin.resize(Gbit + 1);
        int u, v;
        while (input >> u >> v) {
                Graph[u].push_back(v);
                Graph[v].push_back(u);
        }
}


void read_graph_bin(const char* BinFile) {
    int fd = open(BinFile, O_RDONLY);
    if (fd < 0) { cerr << "Cannot open bin: " << BinFile << endl; exit(1); }
    struct stat st; fstat(fd, &st);
    size_t fsize = st.st_size;
    char* base = (char*)mmap(nullptr, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) { cerr << "mmap failed: " << BinFile << endl; exit(1); }
    if (fsize < 20) { cerr << "bin too small: " << BinFile << endl; exit(1); }
    uint32_t magic; int64_t nn, ne;
    memcpy(&magic, base, 4); memcpy(&nn, base + 4, 8); memcpy(&ne, base + 12, 8);

    const uint32_t MAGIC_EDGE = 0xB0817A03u;
    const uint32_t MAGIC_ORICSR = 0x0817C5A0u;
    if (magic == MAGIC_ORICSR) {
        if (fsize != 28 + (size_t)nn * 4 + (size_t)ne * 4) {
            cerr << "oricsr header mismatch: " << BinFile << endl; exit(1);
        }

        int64_t hdrEd; memcpy(&hdrEd, base + 20, 8);
        nodenum = (int)nn;
        edgenum = (int)hdrEd;
        const uint32_t* deg = (const uint32_t*)(base + 28);
        const uint32_t* nbr = deg + nn;
        visited.assign(nodenum, -1);
        cnt.assign(nodenum, 0);
        GraphBit.resize(nodenum);
        Verts.resize(nodenum);
        for (int i = 0; i < nodenum; i++) Verts[i] = i;
        pos.resize(nodenum);
        bin.resize(Gbit + 1);
        Graph.resize(nodenum);
        size_t off2 = 0;
        for (int i = 0; i < nodenum; i++) {
            Graph[i].assign(nbr + off2, nbr + off2 + deg[i]);
            off2 += deg[i];
        }
        munmap(base, fsize); close(fd);
        return;
    }
    if (magic != MAGIC_EDGE || nn <= 0 || ne < 0 ||
        fsize != 20 + (size_t)nn * 4 + (size_t)ne * 4) {
        cerr << "bin header mismatch: " << BinFile << endl; exit(1);
    }
    nodenum = (int)nn;

    edgenum = (int)(ne / 2);
    const uint32_t* deg = (const uint32_t*)(base + 20);
    const uint32_t* nbr = deg + nn;

    Graph.resize(nodenum);
    visited.assign(nodenum, -1);
    cnt.assign(nodenum, 0);
    GraphBit.resize(nodenum);
    Verts.resize(nodenum);
    for (int i = 0; i < nodenum; i++) Verts[i] = i;
    pos.resize(nodenum);
    bin.resize(Gbit + 1);
    size_t off = 0;
    for (int i = 0; i < nodenum; i++) {
        Graph[i].assign(nbr + off, nbr + off + deg[i]);
        off += deg[i];
    }
    munmap(base, fsize); close(fd);
}

void read_graph(char* GraphFile, char* GraphbitFile) {
        ifstream graphfile(GraphFile), bitfile(GraphbitFile);
        graphfile >> nodenum >> edgenum;
        int u, v;
    Graph.resize(nodenum);
    GraphBit.resize(nodenum);

        for (int i = 1; i <= edgenum; i++) {
                graphfile >> u >> v;
                Graph[u].push_back(v);
                Graph[v].push_back(u);
        }

    std::string bstr;
    int gbit;
    bitfile.read((char *)&gbit, sizeof(int));
        assert(gbit == Gbit);
    for (int i = 0; i < nodenum; i++) {
                bitfile.read((char *)&GraphBit[i], sizeof(GraphBit[i]));
                // sort(graph[i].begin(), graph[i].end());
    }
}



void output_binary(char* filename) {
        ofstream output(filename, ios::out | ios::binary);
        // output << Gbit << endl;
        output.write((char *)&Gbit, sizeof(Gbit));
        for (int i = 0; i < nodenum; i++) {
                // output << GraphBit[i] << endl;
                output.write((char *)&GraphBit[i], sizeof(GraphBit[i]));
        }
}

int MIS(int k) {
        if (k == 0) {
                for (int i = 0; i < nodenum; i++) {
                        int deg = Graph[i].size();
                if (deg > max_degree) max_degree = deg;
                }
        vector<vector<int>> buckets(max_degree + 1);
        for (int v : Verts) {
            int deg = Graph[v].size();
            buckets[deg].push_back(v);
        }
        Verts.clear();
        for (int d = 0; d <= max_degree; ++d) {
            Verts.insert(Verts.end(), buckets[d].begin(), buckets[d].end());
        }
    } else if (k == 1) {
        vector<vector<int>> buckets(max_degree + 1);
        for (int v : Verts) {
            int deg = Graph[v].size();
            buckets[deg].push_back(v);
        }
        Verts.clear();
        for (int d = max_degree; d >= 0; --d) {
            Verts.insert(Verts.end(), buckets[d].begin(), buckets[d].end());
        }
        }
        else {
                vector<vector<int>> cnt_buckets(k + 1);
                for (int v : Verts) {
                        cnt_buckets[cnt[v]].push_back(v);
                }
                Verts.clear();

                for (int c = 0; c <= k; ++c) {
                        Verts.insert(Verts.end(), cnt_buckets[c].begin(), cnt_buckets[c].end());
                }

                // if (k >= 2) assert(is_sorted(Verts.begin(), Verts.end(), [&](int u, int v) {
                //      return cnt[u] < cnt[v];
                // }));

                // if (k == 2) {
                //      for (int i = 0; i < nodenum; i++) ++bin[cnt[i]];
                //      int start = 0;
                //      for (int i = 0; i < k + 1; i++) {
                //              int tmp = bin[i];
                //              bin[i] = start;
                //              start += tmp;
                //      }
                //      for (int i = 0; i < nodenum; i++) {
                //              pos[i] = bin[cnt[i]];
                //              Verts[pos[i]] = i;
                //              ++bin[cnt[i]];
                //      }
                //      for (int i = 0; i < k + 1; i++) bin[i]--;
                // }
                // bin[k] = nodenum - 1;
        }

        for (int i = 0; i < nodenum; i++){
                visited[i] = -1;
        }
        for (int i = 0; i < Verts.size(); i++) {
                if (visited[Verts[i]] == -1) {
                        // if (k >= 2) {
                        //      const int pu = bin[cnt[Verts[i]]];
                        //      const int pv = pos[Verts[i]];
                        //      if (pu != pv) {
                        //              const int u = Verts[pu];
                        //              Verts[pu] = Verts[i];
                        //              pos[Verts[i]] = pu;
                        //              Verts[pv] = u;
                        //              pos[u] = pv;
                        //              i--;
                        //      }
                        //      --bin[cnt[Verts[i]]];
                        // }
                        visited[Verts[i]] = 1;
                        GraphBit[Verts[i]][k] = 1;
                        cnt[Verts[i]] += 1;
                        for (auto w : Graph[Verts[i]]) {
                                if(visited[w] == -1){
                                        visited[w] = 0;
                                }
                        }
                }
        }

        return 0;
}


int check(int u, int v){
        int ans1 = (GraphBit[u] & GraphBit[v]).any();
        int ans2 = -1;
        if (find(Graph[u].begin(), Graph[u].end(), v) != Graph[u].end())
                ans2 = 1;
        else 
                ans2 = 0;
        if (ans1 == 0 && ans2 == 0) return 0; // bit:Y  true:N
        if (ans1 == 0 && ans2 == 1) return 1; // bit:Y  true:Y
        if (ans1 == 1 && ans2 == 0) return 2; // bit:N  true:N
        return 3;
}


const int M = 1e7;

int SampleCheck(){
        srand((unsigned)time(NULL)); 
        int true_positive = 0, false_positive = 0, true_negative = 0;
        for(int i = 0; i < M; i++){
                int u = rand() % nodenum;
                int v = rand() % nodenum;
                while (u == v) v = rand() % nodenum;
                int ans = check(u, v);
                if (ans == 0) false_positive++;
                if (ans == 1) true_negative++;
                if (ans == 2) true_positive++;           
                if (ans == 3) {
                        cout << "BUG" << endl;
                        exit(0);
                }
        }
        // cout << "TP   " <<  true_positive << endl
        //      << "TN   " << true_negative << endl
        //      << "FP  " << false_positive << endl
        //      << "perc        " << false_positive * 100.0 / M << endl;
    printf("%.8lf\n", (M - false_positive) * 100.0 / M);
        return 0;
}

// 1 check 2 index


int main(int argc, char** argv) { 
    if (atoi(argv[1]) == 1) {
        read_graph(argv[2], argv[3]);
        SampleCheck();
    } 
    else if (atoi(argv[1]) == 2) {
        string inpath(argv[2]);
        bool isBin = inpath.size() > 4 && (inpath.substr(inpath.size() - 4) == ".bin" || inpath.substr(inpath.size() - 4) == ".csr");
        if (isBin) read_graph_bin(argv[2]);
        else read_graph(argv[2]);
                auto start = std::chrono::steady_clock::now();
        for (int k = 0; k < Gbit; k++)
            MIS(k);
                auto end = std::chrono::steady_clock::now(); 
                double time_use = std::chrono::duration<double>(end - start).count();
                printf("%f \n", time_use);
                SampleCheck();
        output_binary(argv[3]);
    }
    else cerr << "Error!!!\n";
        return 0;
}
// argv[1] == 2