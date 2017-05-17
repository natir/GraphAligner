#include <sstream>
#include <iostream>
#include <fstream>
#include <functional>
#include <algorithm>
#include <thread>
#include "vg.pb.h"
#include "stream.hpp"
#include "fastqloader.h"
#include "TopologicalSort.h"
#include "SubgraphFromSeed.h"
#include "GraphAligner.h"
#include "mfvs_graph.h"
#include "BigraphToDigraph.h"

class BufferedWriter : std::ostream
{
public:
	class FlushClass {};
	BufferedWriter(std::ostream& stream) : stream(stream) {};
	template <typename T>
	BufferedWriter& operator<<(T obj)
	{
		stringstream << obj;
		return *this;
	}
	BufferedWriter& operator<<(FlushClass f)
	{
		flush();
		return *this;
	}
	void flush()
	{
		stringstream << std::endl;
		stream << stringstream.str();
		stringstream.str("");
	}
	static FlushClass Flush;
private:
	std::ostream& stream;
	std::stringstream stringstream;
};

size_t GraphSizeInBp(const DirectedGraph& graph)
{
	size_t result = 0;
	for (size_t i = 0; i < graph.nodes.size(); i++)
	{
		result += graph.nodes[i].sequence.size();
	}
	return result;
}

size_t GraphSizeInBp(const vg::Graph& graph)
{
	size_t result = 0;
	for (int i = 0; i < graph.node_size(); i++)
	{
		result += graph.node(i).sequence().size();
	}
	return result;
}

vg::Graph mergeGraphs(const std::vector<vg::Graph>& parts)
{
	vg::Graph newGraph;
	std::vector<const vg::Node*> allNodes;
	std::vector<const vg::Edge*> allEdges;
	for (size_t i = 0; i < parts.size(); i++)
	{
		for (int j = 0; j < parts[i].node_size(); j++)
		{
			allNodes.push_back(&parts[i].node(j));
		}
		for (int j = 0; j < parts[i].edge_size(); j++)
		{
			allEdges.push_back(&parts[i].edge(j));
		}
	}
	for (size_t i = 0; i < allNodes.size(); i++)
	{
		auto node = newGraph.add_node();
		node->set_id(allNodes[i]->id());
		node->set_sequence(allNodes[i]->sequence());
		node->set_name(allNodes[i]->name());
	}
	for (size_t i = 0; i < allEdges.size(); i++)
	{
		auto edge = newGraph.add_edge();
		edge->set_from(allEdges[i]->from());
		edge->set_to(allEdges[i]->to());
		edge->set_from_start(allEdges[i]->from_start());
		edge->set_to_end(allEdges[i]->to_end());
		edge->set_overlap(allEdges[i]->overlap());
	}
	return newGraph;
}

int numberOfVerticesOutOfOrder(const DirectedGraph& digraph)
{
	std::map<int, int> ids;
	std::set<int> outOfOrderSet;
	for (size_t i = 0; i < digraph.edges.size(); i++)
	{
		if (digraph.edges[i].toIndex <= digraph.edges[i].fromIndex) outOfOrderSet.insert(digraph.edges[i].toIndex);
	}
	return outOfOrderSet.size();
}

void OrderByFeedbackVertexset(DirectedGraph& graph)
{
	BufferedWriter output { std::cout };
	mfvs::Graph mfvsgraph { graph.nodes.size() };
	for (size_t i = 0; i < graph.nodes.size(); i++)
	{
		mfvsgraph.addVertex(i);
	}
	for (size_t i = 0; i < graph.edges.size(); i++)
	{
		mfvsgraph.addEdge(graph.edges[i].fromIndex, graph.edges[i].toIndex);
	}
	output << "Before the removal of MVFS, is the graph acyclic:" << mfvsgraph.isAcyclic() << "\n";
	auto vertexSetvector = mfvsgraph.minimumFeedbackVertexSet();
	std::set<int> vertexset { vertexSetvector.begin(), vertexSetvector.end() };
	output << "feedback vertex set size: " << vertexSetvector.size() << "\n";
	output << "After the removal of MVFS, is the graph acyclic:" << mfvsgraph.isAcyclic() << BufferedWriter::Flush;
	DirectedGraph graphWithoutVFS { graph };
	graphWithoutVFS.RemoveNodes(vertexset);
	std::vector<size_t> indexOrder = topologicalSort(graphWithoutVFS);
	std::vector<int> nodeIdOrder;
	for (size_t i = 0; i < vertexSetvector.size(); i++)
	{
		nodeIdOrder.push_back(graph.nodes[vertexSetvector[i]].nodeId);
	}
	for (size_t i = 0; i < indexOrder.size(); i++)
	{
		nodeIdOrder.push_back(graphWithoutVFS.nodes[indexOrder[i]].nodeId);
	}
	graph.ReorderByNodeIds(nodeIdOrder);
}

void outputGraph(std::string filename, const vg::Graph& graph)
{
	std::ofstream alignmentOut { filename, std::ios::out | std::ios::binary };
	std::vector<vg::Graph> writeVector {graph};
	stream::write_buffered(alignmentOut, writeVector, 0);
}

std::vector<int> getSinkNodes(const DirectedGraph& graph)
{
	std::set<int> notSinkNodes;
	for (size_t i = 0; i < graph.edges.size(); i++)
	{
		notSinkNodes.insert(graph.nodes[graph.edges[i].fromIndex].nodeId);
	}
	std::vector<int> result;
	for (size_t i = 0; i < graph.nodes.size(); i++)
	{
		if (notSinkNodes.count(graph.nodes[i].nodeId) == 0) result.push_back(graph.nodes[i].nodeId);
	}
	return result;
}

std::vector<int> getSourceNodes(const DirectedGraph& graph)
{
	std::set<int> notSourceNodes;
	for (size_t i = 0; i < graph.edges.size(); i++)
	{
		notSourceNodes.insert(graph.nodes[graph.edges[i].toIndex].nodeId);
	}
	std::vector<int> result;
	for (size_t i = 0; i < graph.nodes.size(); i++)
	{
		if (notSourceNodes.count(graph.nodes[i].nodeId) == 0) result.push_back(graph.nodes[i].nodeId);
	}
	return result;
}

bool GraphEqual(const DirectedGraph& first, const DirectedGraph& second)
{
	if (first.nodes.size() != second.nodes.size()) return false;
	if (first.edges.size() != second.edges.size()) return false;
	for (size_t i = 0; i < first.nodes.size(); i++)
	{
		if (first.nodes[i].nodeId != second.nodes[i].nodeId) return false;
		if (first.nodes[i].originalNodeId != second.nodes[i].originalNodeId) return false;
		if (first.nodes[i].sequence != second.nodes[i].sequence) return false;
		if (first.nodes[i].rightEnd != second.nodes[i].rightEnd) return false;
	}
	for (size_t i = 0; i < first.edges.size(); i++)
	{
		if (first.edges[i].fromIndex != second.edges[i].fromIndex) return false;
		if (first.edges[i].toIndex != second.edges[i].toIndex) return false;
	}
	return true;
}

void replaceDigraphNodeIdsWithOriginalNodeIds(vg::Alignment& alignment, const std::vector<DirectedGraph>& graphs)
{
	std::map<int, int> idMapper;
	for (size_t i = 0; i < graphs.size(); i++)
	{
		for (size_t j = 0; j < graphs[i].nodes.size(); j++)
		{
			if (idMapper.count(graphs[i].nodes[j].nodeId) > 0 && idMapper[idMapper[graphs[i].nodes[j].nodeId]] != graphs[i].nodes[j].originalNodeId)
			{
				std::cerr << "node " << graphs[i].nodes[j].nodeId << " originally inserted as " << idMapper[idMapper[graphs[i].nodes[j].nodeId]] << ", now being inserted as " << graphs[i].nodes[j].originalNodeId << std::endl;
			}
			assert(idMapper.count(graphs[i].nodes[j].nodeId) == 0 || idMapper[idMapper[graphs[i].nodes[j].nodeId]] == graphs[i].nodes[j].originalNodeId);
			idMapper[graphs[i].nodes[j].nodeId] = graphs[i].nodes[j].originalNodeId;
		}
	}
	for (int i = 0; i < alignment.path().mapping_size(); i++)
	{
		int digraphNodeId = alignment.path().mapping(i).position().node_id();
		assert(idMapper.count(digraphNodeId) > 0);
		int originalNodeId = idMapper[digraphNodeId];
		alignment.mutable_path()->mutable_mapping(i)->mutable_position()->set_node_id(originalNodeId);
	}
}

void runComponentMappings(const vg::Graph& graph, const std::vector<const FastQ*>& fastQs, const std::map<const FastQ*, std::vector<vg::Alignment>>& seedhits, std::vector<vg::Alignment>& alignments, int threadnum)
{
	BufferedWriter cerroutput {std::cerr};
	BufferedWriter coutoutput {std::cout};
	for (size_t i = 0; i < fastQs.size(); i++)
	{
		const FastQ* fastq = fastQs[i];
		cerroutput << "thread " << threadnum << " " << i << "/" << fastQs.size() << "\n";
		cerroutput << "read size " << fastq->sequence.size() << "bp" << "\n";
		cerroutput << "components: " << seedhits.at(fastq).size() << BufferedWriter::Flush;
		if (seedhits.at(fastq).size() == 0)
		{
			cerroutput << "read " << fastq->seq_id << " has no seed hits" << BufferedWriter::Flush;
			continue;
		}
		std::vector<std::tuple<int, int, DirectedGraph>> components;
		for (size_t j = 0; j < seedhits.at(fastq).size(); j++)
		{
			cerroutput << "thread " << threadnum << " read " << i << " component " << j << "/" << seedhits.at(fastq).size() << "\n";
			auto seedGraphUnordered = ExtractSubgraph(graph, seedhits.at(fastq)[j], fastq->sequence.size());
			DirectedGraph seedGraph {seedGraphUnordered};
			cerroutput << "component size " << GraphSizeInBp(seedGraph) << "bp" << "\n";
			coutoutput << "out of order before sorting: " << numberOfVerticesOutOfOrder(seedGraph) << "\n";
			int startpos = 0;
			int endpos = 0;
			OrderByFeedbackVertexset(seedGraph);
			bool alreadyIn = false;
			for (size_t k = 0; k < components.size(); k++)
			{
				if (GraphEqual(seedGraph,std::get<2>(components[k])))
				{
					cerroutput << "already exists" << BufferedWriter::Flush;
					alreadyIn = true;
					break;
				}
			}
			if (alreadyIn) continue;
			coutoutput << "out of order after sorting: " << numberOfVerticesOutOfOrder(seedGraph) << BufferedWriter::Flush;
			GraphAligner<uint32_t, int32_t> componentAlignment;
			for (size_t i = 0; i < seedGraph.nodes.size(); i++)
			{
				componentAlignment.AddNode(seedGraph.nodes[i].nodeId, seedGraph.nodes[i].sequence);
			}
			for (size_t i = 0; i < seedGraph.edges.size(); i++)
			{
				componentAlignment.AddEdgeNodeId(seedGraph.nodes[seedGraph.edges[i].fromIndex].nodeId, seedGraph.nodes[seedGraph.edges[i].toIndex].nodeId);
			}
			componentAlignment.Finalize();
			auto forward = componentAlignment.GetLocalAlignmentSequencePosition(fastq->sequence);
			startpos = std::get<1>(forward);
			endpos = std::get<2>(forward);
			components.emplace_back(startpos, endpos, seedGraph);
		}
		std::sort(components.begin(), components.end(), [](auto& left, auto& right) { return std::get<0>(left) < std::get<0>(right); });

		GraphAligner<uint32_t, int32_t> augmentedGraphAlignment;
		std::vector<std::vector<int>> sources;
		std::vector<std::vector<int>> sinks;
		std::vector<DirectedGraph> componentGraphs;
		for (size_t i = 0; i < components.size(); i++)
		{
			auto& g = std::get<2>(components[i]);
			componentGraphs.push_back(g);
			for (size_t j = 0; j < g.nodes.size(); j++)
			{
				augmentedGraphAlignment.AddNode(g.nodes[j].nodeId, g.nodes[j].sequence);
			}
			for (size_t j = 0; j < g.edges.size(); j++)
			{
				augmentedGraphAlignment.AddEdgeNodeId(g.nodes[g.edges[j].fromIndex].nodeId, g.nodes[g.edges[j].toIndex].nodeId);
			}
			sources.emplace_back(getSourceNodes(g));
			sinks.emplace_back(getSinkNodes(g));
		}
		for (size_t i = 0; i < components.size(); i++)
		{
			for (size_t j = i+1; j < components.size(); j++)
			{
				std::vector<int> nodeIdsFromFirst = sinks[i];
				std::vector<int> nodeIdsFromSecond = sources[j];
				for (auto firstId : nodeIdsFromFirst)
				{
					for (auto secondId : nodeIdsFromSecond)
					{
						augmentedGraphAlignment.AddEdgeNodeId(firstId, secondId);
					}
				}
			}
		}
		augmentedGraphAlignment.Finalize();

		cerroutput << "thread " << threadnum << " augmented graph is " << augmentedGraphAlignment.SizeInBp() << "bp" << BufferedWriter::Flush;

		auto alignment = augmentedGraphAlignment.AlignOneWay(fastQs[i]->seq_id, fastQs[i]->sequence, false);

		replaceDigraphNodeIdsWithOriginalNodeIds(alignment, componentGraphs);

		alignments.push_back(alignment);
		cerroutput << "thread " << threadnum << " successfully aligned read " << fastq->seq_id << BufferedWriter::Flush;
		std::vector<vg::Alignment> alignmentvec;
		alignmentvec.emplace_back(alignments.back());
		std::string filename;
		filename = "alignment_";
		filename += std::to_string(threadnum);
		filename += "_";
		filename += fastq->seq_id;
		filename += ".gam";
		std::replace(filename.begin(), filename.end(), '/', '_');
		cerroutput << "write to " << filename << BufferedWriter::Flush;
		std::ofstream alignmentOut { filename, std::ios::out | std::ios::binary };
		stream::write_buffered(alignmentOut, alignmentvec, 0);
		cerroutput << "write finished" << BufferedWriter::Flush;
	}
	cerroutput << "thread " << threadnum << " finished with " << alignments.size() << " alignments" << BufferedWriter::Flush;
}

int main(int argc, char** argv)
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	vg::Graph graph;
	{
		std::cout << "load graph from " << argv[1] << std::endl;
		std::ifstream graphfile { argv[1], std::ios::in | std::ios::binary };
		std::vector<vg::Graph> parts;
		std::function<void(vg::Graph&)> lambda = [&parts](vg::Graph& g) {
			parts.push_back(g);
		};
		stream::for_each(graphfile, lambda);
		graph = mergeGraphs(parts);
		std::cout << "graph is " << GraphSizeInBp(graph) << " bp large" << std::endl;
	}

	auto fastqs = loadFastqFromFile(argv[2]);
	std::cout << fastqs.size() << " reads" << std::endl;

	std::map<std::string, std::vector<vg::Alignment>> seeds;
	{
		std::ifstream seedfile { argv[3], std::ios::in | std::ios::binary };
		std::function<void(vg::Alignment&)> alignmentLambda = [&seeds](vg::Alignment& a) {
			seeds[a.name()].push_back(a);
		};
		stream::for_each(seedfile, alignmentLambda);
	}

	int numThreads = std::stoi(argv[5]);
	std::vector<std::vector<const FastQ*>> readsPerThread;
	std::map<const FastQ*, std::vector<vg::Alignment>> seedHits;
	std::vector<std::vector<vg::Alignment>> resultsPerThread;
	readsPerThread.resize(numThreads);
	resultsPerThread.resize(numThreads);
	int currentThread = 0;
	for (size_t i = 0; i < fastqs.size(); i++)
	{
		if (seeds.count(fastqs[i].seq_id) == 0) continue;
		seedHits[&(fastqs[i])].insert(seedHits[&(fastqs[i])].end(), seeds[fastqs[i].seq_id].begin(), seeds[fastqs[i].seq_id].end());
		readsPerThread[currentThread].push_back(&(fastqs[i]));
		currentThread++;
		currentThread %= numThreads;
	}

	std::vector<std::thread> threads;

	for (int i = 0; i < numThreads; i++)
	{
		threads.emplace_back([&graph, &readsPerThread, &seedHits, &resultsPerThread, i]() { runComponentMappings(graph, readsPerThread[i], seedHits, resultsPerThread[i], i); });
	}

	for (int i = 0; i < numThreads; i++)
	{
		threads[i].join();
	}

	std::vector<vg::Alignment> alignments;

	for (int i = 0; i < numThreads; i++)
	{
		alignments.insert(alignments.end(), resultsPerThread[i].begin(), resultsPerThread[i].end());
	}

	std::cerr << "final result has " << alignments.size() << " alignments" << std::endl;

	std::ofstream alignmentOut { argv[4], std::ios::out | std::ios::binary };
	stream::write_buffered(alignmentOut, alignments, 0);

	return 0;
}
