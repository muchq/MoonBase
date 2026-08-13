package com.muchq.games.one_d4.api.dto;

/**
 * Result of POST /admin/rederive-openings: how many rows were read, and how many actually changed.
 *
 * <p>The two are reported separately because the difference is the answer. A pass that scans the
 * whole table and updates nothing means the stored values already agree with the current
 * derivation, which is what a second run should say — and what a first run says when the caller
 * expected a correction that the derivation does not in fact make.
 */
public record RederiveResponse(int gamesScanned, int gamesUpdated) {}
