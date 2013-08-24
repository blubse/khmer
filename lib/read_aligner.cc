#include "read_aligner.hh"

namespace khmer {
// http://www.wesleysteiner.com/professional/del_fun.html

  Transition get_trans(State s1, State s2) {
    if(s1 == match) {
      if(s2 == match) {
	return MM;
      } else if(s2 == deletion) {
	return MD;
      } else if(s2 == insertion) {
	return MI;
      }
    } else if(s1 == deletion) {
      if(s2 == match) {
	return DM;
      } else if(s2 == deletion) {
	return DD;
      }
    } else if(s1 == insertion) {
      if(s2 == match) {
	return IM;
      } else if(s2 == insertion) {
	return II;
      }
    }

    return disallowed;
  }

  void ReadAligner::enumerate(
		       NodeHeap& open,
		       AlignmentNode* curr,
		       bool forward,
		       const std::string& seq
		       ) {
    int next_seq_idx;
    int remaining;
    int kmerCov;
    double hcost;
    double tsc;
    double sc;
    Transition trans;
    HashIntoType fwd = curr->fwd_hash;
    HashIntoType rc = curr->rc_hash;
    HashIntoType next_fwd, next_rc, hash;
    bool isCorrect;
    unsigned char next_nucl;
    
    AlignmentNode* next;
    
    if (forward) {
      next_seq_idx = curr->seq_idx + 1;
      remaining = seq.size() - next_seq_idx;
    } else {
      next_seq_idx = curr->seq_idx - 1;
      remaining = next_seq_idx;
    }

    //std::cerr << "Enumerating: " << _revhash(fwd, ch->ksize()) << " " << _revhash(rc, ch->ksize()) << std::endl;
    
    // loop for matches and insertions
    for (int i = A; i <= T; i++) {
      next_nucl = nucl_lookup[i];
    
      if(forward) {
	next_fwd = next_f(fwd, next_nucl);
	next_rc = next_r(rc, next_nucl);
      } else {
	next_fwd = prev_f(fwd, next_nucl);
	next_rc = prev_r(rc, next_nucl);
      }
      
      hash = uniqify_rc(next_fwd, next_rc);
      kmerCov = ch->get_count(hash);

      if (kmerCov == 0) {
	continue;
      }

      for(int next_state_iter = match;next_state_iter <= deletion;next_state_iter++) {
	State next_state = static_cast<State>(next_state_iter);
	trans = get_trans(curr->state, next_state);
	hcost = sm->tsc[get_trans(next_state, match)] + sm->tsc[MM] * (remaining - 1);// + sm->match * remaining;
	if(trans == disallowed) {
	  continue;
	}

	if(next_state == match) {
	  if(next_nucl == seq[next_seq_idx]) {
	    sc = sm->match;
	  } else {
	    sc = sm->mismatch;
	  }
	} else {
	  sc = 0;
	}

	if(next_state == match) {
	  next = new AlignmentNode(curr, (Nucl)i, next_seq_idx, (State)next_state,
				   next_fwd, next_rc);	  
	} else if(next_state == deletion) {
	  next = new AlignmentNode(curr, (Nucl)i, curr->seq_idx, (State)next_state,
				   next_fwd, next_rc);
	} else if(next_state == insertion) {
	  next = new AlignmentNode(curr, (Nucl)i, next_seq_idx, (State)next_state,
				   curr->fwd_hash, curr->rc_hash);

	}

	next->score = curr->score + sc + sm->tsc[trans];
	isCorrect = true; //kmerCov >= 1; //isCorrectKmer(nextKmerCov, lambdaOne, lambdaTwo);
	if (!isCorrect) {
	  next->score += errorOffset;
	}
	next->h_score = hcost;
	next->f_score = next->score + next->h_score;
	//std::cerr << "\t" << i << " " << next_nucl << " " << next->score << " " << next->h_score << " " << next->f_score << " " << state_labels[next->state] << " " << trans_labels[trans] << " " << hash << " " << _revhash(hash, ch->ksize()) << " " << _revhash(next_fwd, ch->ksize()) << " " << _revhash(next_rc, ch->ksize()) << " " << kmerCov << std::endl;
	
	if (isCorrect) {
	  open.push(next);
	} else {
	  delete next;
	}
      }
    }
  }

  AlignmentNode* ReadAligner::subalign(AlignmentNode* startVert,
				   unsigned int seqLen,
				   bool forward,
				   const std::string& seq) {
    
    NodeHeap open;
    std::set<AlignmentNode> closed;
    open.push(startVert);
    AlignmentNode* curr;

    while (!open.empty()) {
      curr = open.top();
      //std::cerr << "curr: " << curr->prev << " " << _revhash(curr->fwd_hash, ch->ksize()) << " " << _revhash(curr->rc_hash, ch->ksize()) << " " << curr->base << " " << curr->seq_idx << " " << state_labels[curr->state] << " " << curr->score << " " << curr->f_score << std::endl;
      open.pop();
      
      if (curr->seq_idx == seqLen-1 ||
	  curr->seq_idx == 0) {
	return curr;
      }

      if(set_contains(closed, *curr)) {
	continue;
      }

      closed.insert(*curr);
      
      enumerate(open, curr, forward, seq);
    }
    
    return NULL;
  }

  Alignment* ReadAligner::extract_alignment(AlignmentNode* node, const std::string& read, const std::string& kmer) {
    if(node == NULL) {
      return NULL;
    }

    assert(node->seq_idx < read.length());
    assert(node->seq_idx > 0);
    std::string read_alignment = "";
    std::string graph_alignment = "";
    Alignment* ret = new Alignment;
    ret->score = node->score;
    //std::cerr << "Alignment end: " << node->prev << " " << node->base << " " << node->seq_idx << " " << node->state << " " << node->score << std::endl;

    char read_base;
    char graph_base;

    while(node != NULL && node->prev != NULL) {
      if(node->state == match) {
	graph_base = toupper(nucl_lookup[node->base]);
	read_base = read[node->seq_idx];
      } else if(node->state == insertion) {
	graph_base = '-';
	read_base = tolower(read[node->seq_idx]);
      } else if(node->state == deletion) {
	graph_base = tolower(nucl_lookup[node->base]);
	read_base = '-';
      } else {
	graph_base = '?';
	read_base = '?';
      }
      graph_alignment = graph_base + graph_alignment;
      read_alignment = read_base + read_alignment;
      node = node->prev;
    }
    ret->graph_alignment = kmer + graph_alignment;
    ret->read_alignment = kmer + read_alignment;
    
    return ret;
  }
  
  Alignment* ReadAligner::align_test(const std::string& read) {
    int k = ch->ksize();
    for (unsigned int i = 0; i < read.length() - k + 1; i++) {
      std::string kmer = read.substr(i, k);
      
      int kCov = ch->get_count(kmer.c_str());
      if(kCov > 0) {
	HashIntoType fhash, rhash;
	_hash(kmer.c_str(), k, fhash, rhash);
	//std::cerr << "Starting kmer: " << kmer << " " << _revhash(fhash, ch->ksize()) << " " << _revhash(rhash, ch->ksize()) << " idx: " << i << ", " << i + k - 1 << " emission: " << kmer[k - 1] << std::endl;
	char base = toupper(kmer[k - 1]);
	Nucl e;
	switch(base) {
	case 'A':
	  e = A;
	  break;
	case 'C':
	  e = C;
	  break;
	case 'G':
	  e = G;
	  break;
	case 'T': case 'U':
	  e = T;
	  break;
	}
	AlignmentNode start(NULL, e, i + k - 1, match, fhash, rhash);
	AlignmentNode* end = subalign(&start, read.length(), true, read);
	return extract_alignment(end, read, kmer);
      }
    }
    return NULL;
  }

  Alignment* ReadAligner::align(const std::string& seq,
			   const std::string& kmer,
			   int index) {

    /*AlignmentNode* leftStart = new AlignemntNode(NULL,
						 kmer[0],
						 index,
						 'm',
						 Kmer(kmer));
    AlignmentNode* rightStart = new Node(NULL,
					 kmer[kmer.length()-1],
					 index + kmer.length()-1,
					 'm',
					 Kmer(kmer));
    AlignmentNode* leftGoal = subalign(leftStart,
				       seq.length(),
				       0,
				       leftClosed,
				       leftOpen,
				       seq);
    AlignmentNode* rightGoal = subalign(rightStart,
					seq.length(),
					1,
					rightClosed,
					rightOpen,
					seq);
    
    std::string align = extractString(leftGoal, 0, &readDels) +
      kmer +
      extractString(rightGoal, 1, &readDels);
    
      return Alignment(readDels, align);*/
    return NULL;
  }

  /*Alignment ReadAligner::alignRead(const std::string& read) {
    std::vector<unsigned int> markers;
    bool toggleError = 1;
    
    unsigned int longestErrorRegion = 0;
    unsigned int currentErrorRegion = 0;
    
    unsigned int k = ch->ksize();

    std::set<CandidateAlignment> alignments;
    CandidateAlignment best = CandidateAlignment();

    std::string graphAlign = "";

    for (unsigned int i = 0; i < read.length() - k + 1; i++) {
      std::string kmer = read.substr(i, k);

      assert(kmer.length() == k);
      
      int kCov = ch->get_count(kmer.c_str());
      
      bool isCorrect = isCorrectKmer(kCov, lambdaOne, lambdaTwo);
      
      if (isCorrect && currentErrorRegion) {
	currentErrorRegion = 0;
      }
      
      if (!isCorrect) {
	currentErrorRegion++;
	
	if (currentErrorRegion > longestErrorRegion) {
	  longestErrorRegion = currentErrorRegion;
	}
      }
      
      if (toggleError && isCorrect) {
	markers.push_back(i);
	toggleError = 0;
      } else if (!toggleError && !isCorrect) {
	markers.push_back(i-1);
	toggleError = 1;
      }
    }
    
    // couldn't find a seed k-mer
    if (markers.size() == 0) {
      //std::cout << "Couldn't find a seed k-mer." << std::endl;
      return best;
    }

   // exceeded max error region parameter
    if (longestErrorRegion > maxErrorRegion && maxErrorRegion >= 0) {
      return best;
    }

   // read appears to be error free
    if (markers.size() == 1 && markers[0] == 0) {
      std::map<int,int> readDels;
      CandidateAlignment retAln = CandidateAlignment(readDels, read);
      return retAln;
    }
   
    unsigned int startIndex = 0;

    if (markers[0] != 0) {
      unsigned int index = markers[0];
      Alignment aln = align(ch,
			    read.substr(0, index+k),
			    read.substr(index, k),
			    index);
      
      graphAlign += aln.alignment.substr(0,aln.alignment.length()-k); 
      startIndex++;

      if (markers.size() > 1) {
	graphAlign += read.substr(index, markers[1]-index);
      } else {
	graphAlign += read.substr(index);
      }
    } else {
      graphAlign += read.substr(0, markers[1]-markers[0]);
      startIndex++;
    }

    for (unsigned int i = startIndex; i < markers.size(); i+=2) {
      unsigned int index = markers[i];

      if (i == markers.size()-1) {
	Alignment aln = align(ch,
			      read.substr(index),
			      read.substr(index, k),
			      0);
	graphAlign += aln.alignment.substr(0,aln.alignment.length());
	break;
      } else {
	Alignment aln = align(ch, 
			      read.substr(index, markers[i+1]-index+k),
			      read.substr(index, k),
			      0);
	size_t kmerInd = aln.alignment.rfind(read.substr(markers[i+1], k));
	if (kmerInd == std::string::npos) {
	  return best;
	} else {
	  graphAlign += aln.alignment.substr(0, kmerInd);
	}
      }

      // add next correct region to alignment
      if (i+1 != markers.size()-1) {
	graphAlign += read.substr(markers[i+1], markers[i+2]-markers[i+1]);
      } else {
	graphAlign += read.substr(markers[i+1]);
      }
    }
   
    return NULL;
    }*/
}
