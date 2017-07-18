#include "noether/Image.h"
#include "noether/Network.h"
#include "noether/Nodes.h"
#include "noether/Tensor.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace noether;

void testArray() {
  Array3D<float> X(320, 200, 3);
  X.at(10u, 10u, 2u) = 2;
  assert((X.at(10u, 10u, 2u) == 2) && "Invalid load/store");
}

float delta(float a, float b) { return std::fabs(a - b); }

/// Test the fully connected layer and the softmax function.
/// Example from:
/// http://cs.stanford.edu/people/karpathy/convnetjs/demo/classify2d.html
void testFCSoftMax(bool verbose = false) {
  if (verbose) {
    std::cout << "Testing the softmax layer.\n";
  }

  // Construct the network:
  Network N;
  N.getTrainingConfig().momentum = 0.0;
  auto *A = N.createArrayNode(1, 1, 2);
  auto *FCL0 = N.createFullyConnectedNode(A, 6);
  auto *RL0 = N.createRELUNode(FCL0);
  auto *FCL1 = N.createFullyConnectedNode(RL0, 2);
  auto *RL1 = N.createRELUNode(FCL1);
  auto *SM = N.createSoftMaxNode(RL1);

  // Generate some random numbers in the range -1 .. 1.
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(-1, 1);

  if (verbose) {
    std::cout << "Train the network.\n";
  }

  // Generate lots of samples and learn them.
  for (int iter = 0; iter < 99000; iter++) {
    float x = dis(gen);
    float y = dis(gen);

    // Check if the dot falls within some inner circle.
    float r2 = (x * x + y * y);

    bool InCircle = r2 < 0.6;

    SM->setSelected(InCircle);
    A->getOutput().weight_.at(0, 0, 0) = x;
    A->getOutput().weight_.at(0, 0, 1) = y;
    N.train();
  }

  // Print a diagram that depicts the network decision on a grid.
  if (verbose) {
    for (int x = -10; x < 10; x++) {
      for (int y = -10; y < 10; y++) {
        // Load the inputs:
        A->getOutput().weight_.at(0, 0, 0) = float(x) / 10;
        A->getOutput().weight_.at(0, 0, 1) = float(y) / 10;

        N.infer();
        auto A = SM->getOutput().weight_.at(0, 0, 0);
        auto B = SM->getOutput().weight_.at(0, 0, 1);

        char ch = '=';
        if (A > (B + 0.2)) {
          ch = '+';
        } else if (B > (A + 0.2)) {
          ch = '-';
        }

        std::cout << ch;
      }
      std::cout << "\n";
    }
  }

  if (verbose) {
    std::cout << "Verify the results of the softmax layer.\n";
  }

  // Verify the label for some 10 random points.
  for (int iter = 0; iter < 10; iter++) {
    float x = dis(gen);
    float y = dis(gen);

    float r2 = (x * x + y * y);

    // Throw away confusing samples.
    if (r2 > 0.5 && r2 < 0.7)
      continue;

    // Load the inputs:
    A->getOutput().weight_.at(0, 0, 0) = x;
    A->getOutput().weight_.at(0, 0, 1) = y;

    N.infer();

    // Inspect the outputs:
    if (r2 < 0.50) {
      assert(SM->maxArg() == 1);
    }
    if (r2 > 0.7) {
      assert(SM->maxArg() == 0);
    }
  }
  if (verbose) {
    std::cout << "Done.\n";
  }
}

/// A helper function to load a one-hot vector.
void setOneHot(Array3D<FloatTy> &A, float background, float foreground,
               size_t idx) {
  for (int j = 0; j < A.size(); j++) {
    A.at(0, 0, j) = (j == idx ? foreground : background);
  }
}

void testRegression(bool verbose = false) {
  if (verbose) {
    std::cout << "Testing the regression layer.\n";
  }

  /// This test takes the first element from the input vector, adds one to it
  /// and places the result in the second element of the output vector.
  constexpr int numInputs = 4;

  Network N;
  auto *A = N.createArrayNode(1, 1, numInputs);
  auto *FCL0 = N.createFullyConnectedNode(A, 4);
  auto *RL0 = N.createRELUNode(FCL0);
  auto *RN = N.createRegressionNode(RL0);

  // Train the network:
  for (int iter = 0; iter < 9000; iter++) {
    float target = float(iter % 9);
    setOneHot(A->getOutput().weight_, 0.0, target, 0);
    setOneHot(RN->getExpected(), 0.0, target + 1, 1);

    N.train();
  }
  if (verbose) {
    std::cout << "Verify the result of the regression layer.\n";
  }

  // Test the output:
  for (int iter = 0; iter < 5; iter++) {
    float target = iter % 9 + 1;
    setOneHot(A->getOutput().weight_, 0.0, target, 0);
    setOneHot(RN->getExpected(), 0.0, target + 1, 1);

    N.infer();
    assert(delta(A->getOutput().weight_.at(0, 0, 0) + 1,
                 RN->getOutput().weight_.at(0, 0, 1)) < 0.1);
  }
  if (verbose) {
    std::cout << "Done.\n";
  }
}

void testLearnSingleInput(bool verbose = false) {
  if (verbose) {
    std::cout << "Learning a single input vector.\n";
  }

  Network N;
  N.getTrainingConfig().learningRate = 0.005;
  auto *A = N.createArrayNode(1, 1, 10);
  auto *FCL0 = N.createFullyConnectedNode(A, 10);
  auto *RL0 = N.createRELUNode(FCL0);
  auto *FCL1 = N.createFullyConnectedNode(RL0, 10);
  auto *RL1 = N.createRELUNode(FCL1);
  auto *RN = N.createRegressionNode(RL1);

  // Put in [15, 0, 0, 0, 0 ... ]
  setOneHot(A->getOutput().weight_, 0.0, 15, 0);
  // Expect [0, 9.0, 0 , 0 , ...]
  setOneHot(RN->getExpected(), 0.0, 9.0, 1);

  // Train the network:
  for (int iter = 0; iter < 10000; iter++) {
    N.train();
  }

  if (verbose) {
    std::cout << "Testing the output vector.\n";
  }

  N.infer();

  // Test the output:
  assert(RN->getOutput().weight_.sum() < 10);
  assert(RN->getOutput().weight_.at(0, 0, 1) > 8.5);

  if (verbose) {
    std::cout << "Done.\n";
  }
}

int main() {
  testArray();

  testLearnSingleInput(true);

  testRegression(true);

  testFCSoftMax(true);

  return 0;
}
