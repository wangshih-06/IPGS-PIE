#include "Algorithm/LSystem.h"

#include <QtGlobal>

#include <algorithm>
#include <random>
#include <utility>
#include <vector>

namespace {
QString chooseReplacement(const QVector<ProductionAlternative>& alternatives,
                          std::mt19937& random,
                          bool* found) {
    std::vector<double> weights;
    std::vector<int> indices;
    weights.reserve(static_cast<std::size_t>(alternatives.size()));
    indices.reserve(static_cast<std::size_t>(alternatives.size()));

    for (int index = 0; index < alternatives.size(); ++index) {
        if (alternatives[index].weight > 0.0) {
            weights.push_back(alternatives[index].weight);
            indices.push_back(index);
        }
    }

    if (weights.empty()) {
        *found = false;
        return {};
    }

    std::discrete_distribution<std::size_t> distribution(weights.begin(), weights.end());
    *found = true;
    return alternatives[indices[distribution(random)]].replacement;
}
}

QString LSystem::generate(int iterations) const {
    return generateDetailed(axiom, iterations, randomSeed_).sequence;
}

QString LSystem::generate(const QString& start, int iterations) const {
    return generateDetailed(start, iterations, randomSeed_).sequence;
}

QString LSystem::generate(int iterations, quint32 seed) const {
    return generateDetailed(axiom, iterations, seed).sequence;
}

QString LSystem::generate(const QString& start, int iterations, quint32 seed) const {
    return generateDetailed(start, iterations, seed).sequence;
}

LSystemGenerationResult LSystem::generateDetailed(int iterations) const {
    return generateDetailed(axiom, iterations, randomSeed_);
}

LSystemGenerationResult LSystem::generateDetailed(const QString& start,
                                                   int iterations,
                                                   quint32 seed) const {
    LSystemGenerationResult result;
    result.sequence = start.left(maxGeneratedLength_);
    result.truncated = start.size() > maxGeneratedLength_;
    if (result.truncated) {
        return result;
    }

    std::mt19937 random(seed);
    const int rounds = qMax(0, iterations);

    for (int round = 0; round < rounds; ++round) {
        QString next;
        next.reserve(qMin(maxGeneratedLength_, qMax(32, result.sequence.size() * 2)));

        for (const QChar symbol : result.sequence) {
            QString replacement;
            bool hasReplacement = false;

            const auto stochastic = stochasticRules_.constFind(symbol);
            if (stochastic != stochasticRules_.constEnd()) {
                replacement = chooseReplacement(stochastic.value(), random, &hasReplacement);
            }

            if (!hasReplacement) {
                const auto deterministic = rules.constFind(symbol);
                if (deterministic != rules.constEnd()) {
                    replacement = deterministic.value();
                    hasReplacement = true;
                }
            }

            if (!hasReplacement) {
                replacement = QString(symbol);
            }

            const int remaining = maxGeneratedLength_ - next.size();
            if (replacement.size() > remaining) {
                next += replacement.left(qMax(0, remaining));
                result.truncated = true;
                break;
            }
            next += replacement;
        }

        result.sequence = std::move(next);
        ++result.completedIterations;
        if (result.truncated) {
            break;
        }
    }

    return result;
}

void LSystem::setAxiom(const QString& value) {
    axiom = value;
}

void LSystem::setRule(QChar symbol, const QString& replacement) {
    stochasticRules_.remove(symbol);
    rules.insert(symbol, replacement);
}

bool LSystem::removeRule(QChar symbol) {
    const bool removedDeterministic = rules.remove(symbol) > 0;
    const bool removedStochastic = stochasticRules_.remove(symbol) > 0;
    return removedDeterministic || removedStochastic;
}

void LSystem::addProduction(QChar symbol,
                            const QString& replacement,
                            double weight) {
    rules.remove(symbol);
    stochasticRules_[symbol].push_back({replacement, weight});
}

bool LSystem::clearProductions(QChar symbol) {
    return stochasticRules_.remove(symbol) > 0;
}

QVector<ProductionAlternative> LSystem::productions(QChar symbol) const {
    return stochasticRules_.value(symbol);
}

void LSystem::setRandomSeed(quint32 seed) {
    randomSeed_ = seed;
}

quint32 LSystem::randomSeed() const {
    return randomSeed_;
}

void LSystem::setMaxGeneratedLength(int length) {
    maxGeneratedLength_ = qMax(1, length);
}

int LSystem::maxGeneratedLength() const {
    return maxGeneratedLength_;
}

LSystem LSystem::treePreset() {
    LSystem system;
    system.axiom = QStringLiteral("F");
    system.setRule(QChar('F'), QStringLiteral("FF+[+F-F-F]-[-F+F+F]"));
    return system;
}
