#include "Algorithm/TurtleInterpreter.h"

#include <QStack>
#include <QtMath>

#include <Eigen/Geometry>

#include <algorithm>
#include <random>

namespace {
struct TurtleStackEntry {
    TurtleState state;
    PlantNode* node = nullptr;
};

Vec3 safeDirection(const Vec3& value) {
    return value.squaredNorm() > 1.0e-8f ? value.normalized() : Vec3::UnitY();
}

float randomOffset(std::mt19937& random, float amount) {
    const float range = std::max(0.0f, amount);
    if (range <= 0.0f) {
        return 0.0f;
    }
    return std::uniform_real_distribution<float>(-range, range)(random);
}

float variedScale(std::mt19937& random, float variation) {
    return std::max(0.0f, 1.0f + randomOffset(random, variation));
}
}

PlantNodePtr TurtleInterpreter::interpret(const QString& program,
                                          const PlantRule& rule) const {
    return interpretDetailed(program, rule).root;
}

TurtleInterpretationResult TurtleInterpreter::interpretDetailed(
    const QString& program,
    const PlantRule& rule) const {
    TurtleInterpretationResult result;
    result.stats.programLength = program.size();

    const float minimumLength = std::max(0.000001f, rule.minimumLength);
    const float minimumRadius = std::max(0.000001f, rule.minimumRadius);
    const Vec3 initialDirection = safeDirection(rule.direction);
    result.root = std::make_unique<PlantNode>(rule.origin,
                                              initialDirection,
                                              std::max(minimumRadius, rule.radius),
                                              0.0f,
                                              0);

    TurtleState state;
    state.position = rule.origin;
    state.length = std::max(minimumLength, rule.length);
    state.radius = std::max(minimumRadius, rule.radius);
    state.generation = 0;
    state.orientation = orientationFromDirection(initialDirection);
    state.direction = initialDirection;

    PlantNode* currentNode = result.root.get();
    QStack<TurtleStackEntry> stack;
    std::mt19937 random(rule.randomSeed);

    const auto sampledAngle = [&]() {
        return std::max(0.0f,
                        rule.angleDegrees +
                            randomOffset(random, rule.angleVariationDegrees));
    };

    for (const QChar symbol : program) {
        switch (symbol.toLatin1()) {
        case 'F':
        case 'G': {
            state.direction = safeDirection(state.orientation * Vec3::UnitY());
            const float segmentLength = std::max(
                minimumLength,
                state.length * variedScale(random, rule.lengthVariation));
            const float segmentRadius = std::max(
                minimumRadius,
                state.radius * variedScale(random, rule.radiusVariation));
            const Vec3 end = state.position + state.direction * segmentLength;
            const PlantNodeType type = state.generation == 0
                                           ? PlantNodeType::Stem
                                           : PlantNodeType::Branch;
            currentNode = currentNode->addChild(end,
                                                 state.direction,
                                                 segmentRadius,
                                                 segmentLength,
                                                 state.generation,
                                                 -1,
                                                 0.0f,
                                                 true,
                                                 type);
            state.position = end;
            if (rule.decayLengthOnForward) {
                state.length = std::max(minimumLength,
                                        state.length * std::max(0.0f, rule.lengthScale));
            }
            if (rule.decayRadiusOnForward) {
                state.radius = std::max(minimumRadius,
                                        state.radius * std::max(0.0f, rule.radiusScale));
            }
            ++result.stats.segmentCount;
            break;
        }
        case 'f':
            state.direction = safeDirection(state.orientation * Vec3::UnitY());
            state.position += state.direction *
                              std::max(minimumLength,
                                       state.length * variedScale(random, rule.lengthVariation));
            if (rule.decayLengthOnForward) {
                state.length = std::max(minimumLength,
                                        state.length * std::max(0.0f, rule.lengthScale));
            }
            ++result.stats.moveCount;
            break;
        case '+':
            rotateLocal(state, Vec3::UnitZ(), sampledAngle());
            ++result.stats.rotationCount;
            break;
        case '-':
            rotateLocal(state, Vec3::UnitZ(), -sampledAngle());
            ++result.stats.rotationCount;
            break;
        case '&':
            rotateLocal(state, Vec3::UnitX(), sampledAngle());
            ++result.stats.rotationCount;
            break;
        case '^':
            rotateLocal(state, Vec3::UnitX(), -sampledAngle());
            ++result.stats.rotationCount;
            break;
        case '\\':
            rotateLocal(state, Vec3::UnitY(), sampledAngle());
            ++result.stats.rotationCount;
            break;
        case '/':
            rotateLocal(state, Vec3::UnitY(), -sampledAngle());
            ++result.stats.rotationCount;
            break;
        case '|':
            rotateLocal(state, Vec3::UnitZ(), 180.0f);
            ++result.stats.rotationCount;
            break;
        case '!':
            state.radius = std::max(minimumRadius,
                                    state.radius * std::max(0.0f, rule.radiusScale));
            break;
        case '[':
            stack.push({state, currentNode});
            ++state.generation;
            ++result.stats.pushCount;
            result.stats.maximumStackDepth =
                std::max(result.stats.maximumStackDepth, stack.size());
            break;
        case ']':
            if (!stack.isEmpty()) {
                const TurtleStackEntry saved = stack.pop();
                state = saved.state;
                currentNode = saved.node;
                ++result.stats.popCount;
            } else {
                ++result.stats.unmatchedClosingBrackets;
            }
            break;
        case 'L':
            ++result.stats.leafMarkers;
            break;
        case 'R':
            ++result.stats.rootMarkers;
            break;
        case 'B':
            ++result.stats.budMarkers;
            break;
        case 'X':
        case 'Y':
        case 'A':
            // Non-drawing variables are commonly used by production rules.
            break;
        default:
            ++result.stats.ignoredSymbols;
            break;
        }
    }

    result.stats.unclosedBranches = stack.size();
    return result;
}

Quat TurtleInterpreter::orientationFromDirection(const Vec3& direction) {
    const Vec3 target = safeDirection(direction);
    const Vec3 forward = Vec3::UnitY();
    if ((target - forward).squaredNorm() < 1.0e-8f) {
        return Quat::Identity();
    }
    if ((target + forward).squaredNorm() < 1.0e-8f) {
        return Quat(Eigen::AngleAxisf(static_cast<float>(M_PI), Vec3::UnitX()));
    }
    return Quat::FromTwoVectors(forward, target).normalized();
}

void TurtleInterpreter::rotateLocal(TurtleState& state,
                                    const Vec3& localAxis,
                                    float degrees) {
    const float radians = qDegreesToRadians(degrees);
    state.orientation =
        (state.orientation *
         Quat(Eigen::AngleAxisf(radians, localAxis.normalized())))
            .normalized();
    state.direction = safeDirection(state.orientation * Vec3::UnitY());
}
