#pragma once

#include <QMap>
#include <QString>
#include <QVector>
#include <QtGlobal>

// One weighted replacement for a symbol. Alternatives with non-positive
// weights are ignored during generation.
struct ProductionAlternative {
    QString replacement;
    double weight = 1.0;
};

struct LSystemGenerationResult {
    QString sequence;
    int completedIterations = 0;
    bool truncated = false;
};

// Context-free deterministic/stochastic string rewriting used by the plant
// generator. The public rules map is retained for compatibility with the
// original deterministic API; weighted productions take precedence.
class LSystem {
public:
    QString axiom = QStringLiteral("F");
    QMap<QChar, QString> rules;

    QString generate(int iterations) const;
    QString generate(const QString& start, int iterations) const;
    QString generate(int iterations, quint32 seed) const;
    QString generate(const QString& start, int iterations, quint32 seed) const;

    LSystemGenerationResult generateDetailed(int iterations) const;
    LSystemGenerationResult generateDetailed(const QString& start,
                                              int iterations,
                                              quint32 seed) const;

    void setAxiom(const QString& value);
    void setRule(QChar symbol, const QString& replacement);
    bool removeRule(QChar symbol);

    void addProduction(QChar symbol,
                       const QString& replacement,
                       double weight = 1.0);
    bool clearProductions(QChar symbol);
    QVector<ProductionAlternative> productions(QChar symbol) const;

    void setRandomSeed(quint32 seed);
    quint32 randomSeed() const;

    void setMaxGeneratedLength(int length);
    int maxGeneratedLength() const;

    static LSystem treePreset();

private:
    QMap<QChar, QVector<ProductionAlternative>> stochasticRules_;
    quint32 randomSeed_ = 5489u;
    int maxGeneratedLength_ = 2000000;
};
