/*
 * opencog/learning/moses/moses/scoring.h
 *
 * Copyright (C) 2002-2008 Novamente LLC
 * All Rights Reserved
 *
 * Written by Moshe Looks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _MOSES_SCORING_H
#define _MOSES_SCORING_H

#include <iostream>
#include <fstream>
#include <functional>

#include <boost/range/numeric.hpp>
#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/weighted_skewness.hpp>

#include <opencog/util/lru_cache.h>
#include <opencog/util/algorithm.h>
#include <opencog/util/functional.h>
#include <opencog/util/KLD.h>

#include <opencog/comboreduct/reduct/reduct.h>
#include <opencog/comboreduct/combo/eval.h>
#include <opencog/comboreduct/combo/table.h>
#include <opencog/comboreduct/reduct/meta_rules.h>

#include "using.h"
#include "../representation/representation.h"
#include "types.h"

namespace opencog { namespace moses {

typedef float fitness_t; /// @todo is that really useful?

// Abstract scoring function class to implement
struct score_base : public unary_function<combo_tree, score_t>
{
    // Evaluate the candidate tr
    virtual score_t operator()(const combo_tree& tr) const = 0;
    
    // Return the best possible score achievable with that fitness
    // function. This is useful in order to stop running MOSES when
    // the best possible score is reached.
    virtual score_t best_possible_score() const = 0;
};

// Abstract bscoring function class to implement
struct bscore_base : public unary_function<combo_tree, behavioral_score>
{
    // Evaluate the candidate tr
    virtual behavioral_score operator()(const combo_tree& tr) const = 0;
    
    // Return the best possible bscore achievable with that fitness
    // function. This is useful in order to stop running MOSES when
    // the best possible score is reached
    virtual behavioral_score best_possible_bscore() const = 0;
};

/**
 * score calculated based on the behavioral score. Useful to avoid
 * redundancy of code and computation in case there is a cache over
 * bscore. The score is calculated as the sum of the bscore over all
 * features, that is:
 * score = sum_f BScore(f),
 */
/// @todo Inheriting that class from score_base raises a compile error
/// because in moses_exec.h some code attempts to use BScore that does
/// does not contain best_possible_bscore(). Specifically, the 
/// BScoreACache and ScoreACache typedefs do this. However, this begs
/// a question: why do we need a base class anyway, if we're not going
/// to need it? ??

template<typename BScore>
struct bscore_based_score : public unary_function<combo_tree, score_t>
{
    bscore_based_score(const BScore& bs) : bscore(bs) {}
    score_t operator()(const combo_tree& tr) const
    {
        try {
            behavioral_score bs = bscore(tr);
            score_t res = std::accumulate(bs.begin(), bs.end(), 0.0);

            if (logger().getLevel() < Logger::FINE) {
                stringstream ss;
                ss << "bscore_based_score: " << res;
                ss << " for candidate: " << tr;
                logger().fine(ss.str());
            }

            return res;
        }
        catch (EvalException& ee)
        {
            // Exceptions are raised when operands are out of their
            // valid domain (negative input log or division by zero),
            // or outputs a value which is not representable (too
            // large exp or log). The error is logged as level fine
            // because its happens very often when learning continuous
            // functions, and it gets too much in the way if logged at
            // a lower level.
            stringstream ss;
            ss << "The following candidate: " << tr << "\n";
            ss << "has failed to be evaluated, "
               << "raising the following exception: "
               << ee.get_message() << " " << ee.get_vertex();
            logger().fine(ss.str());

            return get_score(worst_composite_score);
        }
    }

    // Returns the best score reachable for that problem. Used as
    // termination condition.
    score_t best_possible_score() const
    {
        return boost::accumulate(bscore.best_possible_bscore(), 0.0);
    }
    const BScore& bscore;
};

/**
 * Bscore defined by multiple scoring functions. This is done when the
 * problem to solve is defined in terms of multiple problems. For now
 * the multiple scores have the same type as defined by the template
 * argument Score.
 */
template<typename Score>
struct multiscore_based_bscore : public bscore_base
{
    typedef boost::ptr_vector<Score> ScoreSeq;

    // ctors
    multiscore_based_bscore(const ScoreSeq& scores_) : scores(scores_) {}

    // main operator
    behavioral_score operator()(const combo_tree& tr) const
    {
        behavioral_score bs(scores.size());
        boost::transform(scores, bs.begin(), [&](const Score& sc){return sc(tr);});
        return bs;
    }

    behavioral_score best_possible_bscore() const
    {
        behavioral_score bs;
        foreach(const Score& sc, scores) {
            bs.push_back(sc.best_possible_score());
        }
        return bs;
    }
    
    ScoreSeq scores;
};

/**
 * Bscore defined by multiple behavioral scoring functions. This is
 * done when the problem to solve is defined in terms of multiple
 * problems. For now the multiple scores have the same type as defined
 * by the template argument Score.
 */
template<typename BScore>
struct multibscore_based_bscore : public bscore_base
{
    typedef boost::ptr_vector<BScore> BScoreSeq;

    // ctors
    multibscore_based_bscore(const BScoreSeq& bscores_) : bscores(bscores_) {}

    // main operator
    behavioral_score operator()(const combo_tree& tr) const
    {
        behavioral_score bs;
        foreach(const BScore& bsc, bscores)
            boost::push_back(bs, bsc(tr));
        return bs;
    }

    behavioral_score best_possible_bscore() const
    {
        behavioral_score bs;
        foreach(const BScore& bsc, bscores)
            boost::push_back(bs, bsc.best_possible_bscore());
        return bs;
    }
    
    BScoreSeq bscores;
};

/**
 * Each feature corresponds to an input tuple, 0 if the output of the
 * candidate matches the output of the intended function, -1
 * otherwise.
 */
struct logical_bscore : public bscore_base
{
    template<typename Func>
    logical_bscore(const Func& func, int a)
            : target(func, a), arity(a) {}
    logical_bscore(const combo_tree& tr, int a)
            : target(tr, a), arity(a) {}

    behavioral_score operator()(const combo_tree& tr) const;

    behavioral_score best_possible_bscore() const;
    
    complete_truth_table target;
    int arity;
};

// Used to define the complexity scoring component given that p is the
// probability of having an observation being wrong (see the comment
// regarding ctruth_table_bscore for more information).
score_t discrete_complexity_coef(unsigned alphabet_size, double p);

// Used to define the complexity scoring component given that stdev is
// the standard deviation of the noise of the we're trying to predict
// output (see the comment regarding contin_bscore for more
// information).
score_t contin_complexity_coef(unsigned alphabet_size, double stdev);
        
/**
 * Fitness function based on discretization of the output. If the
 * classes match the bscore element is 0, or -1 otherwise. If
 * @weighted_average is true then each element of the bscore is
 * weighted so that each class overall has the same weight in the
 * scoring function.
 *
 * The Occam's razor function is identical to ctruth_table_bscore
 */
struct discretize_contin_bscore : public bscore_base
{
    discretize_contin_bscore(const OTable& ot, const ITable& it,
                             const vector<contin_t>& thres,
                             bool weighted_average,
                             float alphabet_size, float p,
                             RandGen& _rng);

    // @todo when switching to gcc 4.6 use constructor delagation to
    // simplify that
    // discretize_contin_bscore(const Table& table,
    //                          const vector<contin_t>& thres,
    //                          bool weighted_average,
    //                          float alphabet_size, float p,
    //                          RandGen& _rng);

    behavioral_score operator()(const combo_tree& tr) const;

    // The best possible bscore is a vector of zeros. That's probably
    // not quite true, because there could be duplicated inputs, but
    // that's acceptable for now.
    behavioral_score best_possible_bscore() const;
    
    OTable target;
    ITable cit;
    vector<contin_t> thresholds;
    bool weighted_accuracy;     // Whether the bscore is weighted to
                                // deal with unbalanced data.
    bool occam;                 // Whether Occam's razor is enabled
    score_t complexity_coef;
    RandGen& rng;

protected:
    // Return the index of the class of value v.
    size_t class_idx(contin_t v) const;
    // Like class_idx but assume that the value v is within the class
    // [l_idx, u_idx)
    size_t class_idx_within(contin_t v, size_t l_idx, size_t u_idx) const;

    vector<size_t> classes;       // classes of the output, alligned with target

    // Weight of each class, so that each one weighs as much as the
    // others, even in case of unbalance sampling. Specifically:
    // weights[i] = s / (n * c_i) where s is the sample size, n the
    // number of classes and c_i the number of samples for class i.
    vector<score_t> weights;
};

/**
 * Behavioral scoring function minimizing residual errors.
 *
 * The first elements of the bscore correspond to the minus squared
 * errors. The last element is optional and corresponds to an program
 * size penalty.
 *
 * The math justifying the program size penalty equations is based on
 * the following thread
 * http://groups.google.com/group/opencog-news/browse_thread/thread/b7704419e082c6f1
 *
 * Here's a summary:
 *
 * According to Bayes
 * dP(M|D) = dP(D|M) * P(M) / P(D)
 *
 * Now let's consider the log likelihood of M knowing D, since D is
 * constant we can ignore P(D), so:
 * LL(M) = log(dP(D|M)) + log(P(M))
 * 
 * Assume the output of M on input x has a Guassian noise of mean M(x)
 * and variance v, so dP(D|M) (the density probability)
 * dP(D|M) = Prod_{x\in D} (2*Pi*v)^(-1/2) exp(-(M(x)-D(x))^2/(2*v))
 *
 * Assume
 * P(M) = |A|^-|M|
 * where |A| is the alphabet size.
 *
 * After simplification we can get the following log-likelihood of dP(M|D)
 * -|M|*log(|A|)*2*v - Sum_{x\in D} (M(x)-D(x))^2
 *
 * Each datum corresponds to a feature of the bscore.
 *
 * |M|*log(|A|)*2*v corresponds to an additional feature when v > 0
 */
struct contin_bscore : public bscore_base
{
    template<typename Scoring>
    contin_bscore(const Scoring& score, const ITable& r,
                  float alphabet_size, float stdev,
                  RandGen& _rng)
        : target(score, r), cti(r), rng(_rng)
    {
        occam = stdev > 0;
        set_complexity_coef(alphabet_size, stdev);
    }

    // @todo when switching to gcc 4.6 use constructor delagation to
    // simplify that
    contin_bscore(const OTable& t, const ITable& r,
                  float alphabet_size, float stdev,
                  RandGen& _rng)
        : target(t), cti(r), rng(_rng)
    {
        occam = stdev > 0;
        set_complexity_coef(alphabet_size, stdev);
    }

    // @todo when switching to gcc 4.6 use constructor delagation to
    // simplify that
    contin_bscore(const Table& table,
                  float alphabet_size, float stdev,
                  RandGen& _rng)
        : target(table.otable), cti(table.itable), rng(_rng)
    {
        occam = stdev > 0;
        set_complexity_coef(alphabet_size, stdev);
    }

    behavioral_score operator()(const combo_tree& tr) const;

    // The best possible bscore is a vector of zeros. That's probably
    // not quite true, because there could be duplicated inputs, but
    // that's acceptable for now.
    behavioral_score best_possible_bscore() const;
    
    OTable target;
    ITable cti;
    bool occam;
    score_t complexity_coef;
    RandGen& rng;

private:
    void set_complexity_coef(float alphabet_size, float stdev);
};

/**
 * Like contin_bscore but for boolean.
 *
 * The CTable ctt holds the "compressed" data table, consisting of
 * rows of input (independent) variables, and a single output
 * (dependent) variable. Scoring is performed by evaluating the
 * combo tree for each input row, and comparing the evaluation results
 * to the output column.
 *
 * The first elements correspond to the minus absolute errors (0 if
 * the booleans fit, -1 if they don't). The last element is optional
 * and corresponds to a program size penalty.
 *
 * Regarding the program size penalty, instead of considering a
 * standard deviation of the output, the probability p that one datum
 * is wrong is used.
 *
 * The details are in this thread
 * http://groups.google.com/group/opencog/browse_thread/thread/a4771ecf63d38df
 *
 * Briefly after reduction of
 * LL(M) = -|M|*log(|A|) + Sum_{x\in D1} log(p) + Sum_{x\in D2} log(1-p)
 *
 * one gets the following log-likelihood
 * |M|*log|A|/log(p/(1-p)) - |D1|
 * with p<0.5 and |D1| the number of outputs that match
 */
struct ctruth_table_bscore : public bscore_base
{
    ctruth_table_bscore(const CTable& _ctt,
                        float alphabet_size, float p,
                        RandGen& _rng);

    behavioral_score operator()(const combo_tree& tr) const;

    // Return the best possible bscore. Used as one of the
    // termination conditions (when the best bscore is reached).
    behavioral_score best_possible_bscore() const;
    
    mutable CTable ctt;         // mutable because accessing a missing
                                // element adds it in the map.
    bool occam; // If true, then Occam's razor is taken into account.
    score_t complexity_coef;
    RandGen& rng;
};

// Bscore to find interesting predicates. Interestingness is measured
// in terms of several features such as
//
//    1) the Kullback Leibler divergence between the distribution
// output of the dataset and the distribution over the output filtered
// in by the program (when the predicate is true).
//    2) the difference in skewness the 2 distributions
//    3) the standardized Mann-Whitney U statistic
//    4) the product of #2 and #3
//    5) the log of the entropy of the predicate
//
// All those features are weighted, any one with null weight is
// disabled (it isn't computed and isn't pushed in the bscore).
struct interesting_predicate_bscore : public bscore_base
{
    typedef score_t weight_t;
    typedef Counter<contin_t, contin_t> counter_t;
    typedef Counter<contin_t, contin_t> pdf_t;
    typedef boost::accumulators::accumulator_set<contin_t,
                                                 boost::accumulators::stats<
                      boost::accumulators::tag::weighted_skewness
                                                     >, contin_t> accumulator_t;

    interesting_predicate_bscore(const CTable& ctable,
                                 float alphabet_size, float stdev,
                                 RandGen& _rng,
                                 weight_t kld_weight = 1.0,
                                 weight_t skewness_weight = 1.0,
                                 weight_t stdU_weight = 1.0,
                                 weight_t skew_U_weight = 1.0,
                                 weight_t log_entropy_weight = 1.0,
                                 bool decompose_kld = false);
    behavioral_score operator()(const combo_tree& tr) const;

    // the KLD has no upper boundary so the best of possible score is
    // the maximum value a behavioral_score can represent
    behavioral_score best_possible_bscore() const;

    counter_t counter; // counter of the unconditioned distribution
    pdf_t pdf;     // pdf of the unconditioned distribution
    mutable KLDS<contin_t> klds;
    CTable ctable;
    bool occam;
    score_t complexity_coef;
    contin_t skewness;   // skewness of the unconditioned distribution
    RandGen& rng;

    // weights of the various features
    weight_t kld_w;
    weight_t skewness_w;
    weight_t stdU_w;
    weight_t skew_U_w;
    weight_t log_entropy_w;
    // If true then each component of the computation of KLD
    // corresponds to an element of the bscore. Otherwise the whole
    // KLD occupies just one bscore element
    bool decompose_kld;

private:
    void set_complexity_coef(float alphabet_size, float stdev);
};

// For testing only
struct dummy_score : public unary_function<combo_tree, score_t>
{
    score_t operator()(const combo_tree& tr) const {
        return score_t();
    }
};

// For testing only
struct dummy_bscore : public unary_function<combo_tree, behavioral_score>
{
    behavioral_score operator()(const combo_tree& tr) const {
        return behavioral_score();
    }
};

/**
 * Mostly for testing the optimization algos.  Returns minus the
 * hamming distance of the candidate to a given target instance and
 * constant null complexity.
 */
struct distance_based_scorer : public unary_function<instance,
                                                     composite_score>
{
    distance_based_scorer(const field_set& _fs,
                          const instance& _target_inst)
        : fs(_fs), target_inst(_target_inst) {}

    composite_score operator()(const instance& inst) const {
        score_t sc = -fs.hamming_distance(target_inst, inst);
        // Logger
        if(logger().getLevel() >= Logger::FINE) {
            stringstream ss;
            ss << "distance_based_scorer - Evaluate instance: " 
               << fs.stream(inst) << std::endl << "Score = " << sc << std::endl;
            logger().fine(ss.str());
        }
        // ~Logger
        return composite_score(sc, 0);
    }

protected:
    const field_set& fs;
    const instance& target_inst;
};

template<typename Scoring>
struct complexity_based_scorer : public unary_function<instance,
                                                       composite_score>
{
    complexity_based_scorer(const Scoring& s, representation& rep, bool reduce)
        : score(s), _rep(rep), _reduce(reduce) {}

    composite_score operator()(const instance& inst) const
    {
        using namespace reduct;

        // Logger
        if (logger().getLevel() >= Logger::FINE) {
            stringstream ss;
            ss << "complexity_based_scorer - Evaluate instance: " 
               << _rep.fields().stream(inst);
            logger().fine(ss.str());
        }
        // ~Logger

        try {
            combo_tree tr = _rep.get_candidate(inst, _reduce);
            return composite_score(score(tr), complexity(tr));
        } catch (...) {
            stringstream ss;
            ss << "The following instance has failed to be evaluated: " 
               << _rep.fields().stream(inst);
            logger().fine(ss.str());
            return worst_composite_score;
        }
    }

protected:
    const Scoring& score;
    representation& _rep;
    bool _reduce; // whether the exemplar is reduced before being
                  // evaluated, this may be advantagous if Scoring is
                  // also a cache
};

} //~namespace moses
} //~namespace opencog

#endif
