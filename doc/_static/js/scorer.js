/**
 * Simple search result scoring code.
 *
 * Copyright 2007-2018 by the Sphinx team
 * Copyright (c) 2019, Intel
 * SPDX-License-Identifier: Apache-2.0
 */

var Scorer = {
  // Implement the following function to further tweak the score for
  // each result The function takes a result array [filename, title,
  // anchor, descr, score] and returns the new score.

  // For Zephyr search results, push display down for kconfig, boards,
  // and samples so "regular" docs will show up before them

  score: function(result) {
    var baseScore = result[4];
    
    // Apply path-based adjustments first
    if (result[0].search("reference/kconfig/")>=0) {
       baseScore = -5;
    }
    else if (result[0].search("boards/")>=0) {
       baseScore = -5;
    }
    else if (result[0].search("samples/")>=0) {
       baseScore = -5;
    }
    
    // Apply field length normalization
    // Use description field (result[3]) length as a proxy for document length
    // Shorter documents with matches are typically more relevant
    var description = result[3] || "";
    var fieldLength = description.length;
    
    // Only apply normalization for positive scores and non-empty descriptions
    if (baseScore > 0 && fieldLength > 0) {
      // Use an average field length of 200 characters as baseline
      // Apply square root normalization to avoid over-penalizing long documents
      var avgLength = 200;
      var normFactor = Math.sqrt(avgLength / fieldLength);
      // Clamp the normalization factor to reasonable bounds (0.5 to 2.0)
      // This prevents extreme adjustments for very short or very long documents
      normFactor = Math.max(0.5, Math.min(2.0, normFactor));
      baseScore = baseScore * normFactor;
    }
    
    return baseScore;
  },


  // query matches the full name of an object
  objNameMatch: 11,
  // or matches in the last dotted part of the object name
  objPartialMatch: 6,
  // Additive scores depending on the priority of the object
  objPrio: {0:  15,   // used to be importantResults
            1:  5,   // used to be objectResults
            2: -5},  // used to be unimportantResults
  //  Used when the priority is not in the mapping.
  objPrioDefault: 0,

  // query found in title
  title: 15,
  // query found in terms
  term: 5
};
