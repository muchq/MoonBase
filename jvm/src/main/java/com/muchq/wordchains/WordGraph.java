package com.muchq.wordchains;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class WordGraph {
    private final List<String> nodes;
    private final Map<String, List<Integer>> edges;

    public WordGraph(List<String> nodes) {
        this.nodes = nodes;
        this.edges = new HashMap<>();
    }
}
