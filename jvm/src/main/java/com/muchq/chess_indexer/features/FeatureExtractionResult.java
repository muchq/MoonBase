package com.muchq.chess_indexer.features;

import com.muchq.chess_indexer.model.GameFeatures;
import java.util.List;

public record FeatureExtractionResult(GameFeatures features, List<Motif> motifs) {}
