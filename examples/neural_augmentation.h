#ifndef NEURAL_AUGMENTATION_H
#define NEURAL_AUGMENTATION_H

#include "neural_scalar.h"
#include "tiny_ceres_estimator.h"

struct NeuralAugmentation {
  std::vector<TinyNeuralNetworkSpecification> specs;

  std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>
      output_inputs;

  static inline int default_hidden_layers{2};
  static inline int default_hidden_units{5};
  TinyNeuralNetworkActivation activation_fn = NN_ACT_ELU;

  // L1 regularization for input weights (lasso) to encourage sparse inputs
  double input_lasso_regularization{0};
  // L2 regularization term for upper layers
  double upper_l2_regularization{1};

  double weight_limit = 0.1;
  double bias_limit = 0.2;

  TinyNeuralNetworkSpecification &add_wiring(
      const std::string &output, const std::vector<std::string> &inputs,
      int hidden_layers = default_hidden_layers,
      int hidden_units = default_hidden_units,
      TinyNeuralNetworkActivation output_fn = NN_ACT_IDENTITY) {
    return add_wiring(std::vector<std::string>{output}, inputs, hidden_layers, hidden_units, output_fn);
  }

  TinyNeuralNetworkSpecification &add_wiring(
      const std::vector<std::string> &outputs,
      const std::vector<std::string> &inputs,
      int hidden_layers = default_hidden_layers,
      int hidden_units = default_hidden_units,
      TinyNeuralNetworkActivation output_fn = NN_ACT_IDENTITY) {
    // TODO consider allowing biases on inputs?
    TinyNeuralNetworkSpecification spec(static_cast<int>(inputs.size()), false);
    // define overparameterized NN
    for (int li = 0; li < hidden_layers; ++li) {
      spec.add_linear_layer(activation_fn, hidden_units);
    }
    // output layer
    spec.add_linear_layer(NN_ACT_IDENTITY, static_cast<int>(outputs.size()));
    output_inputs.push_back(std::make_pair(outputs, inputs));
    specs.push_back(spec);
    return specs.back();
  }

  template <typename Scalar, typename Utils>
  void instantiate(const std::vector<Scalar> &params,
                   std::size_t param_index_offset = 0) const {
    static_assert(
        !is_neural_scalar<Scalar, Utils>::value,
        "Parameter vector provided to NeuralAugmentation::instantiate() must "
        "not be of NeuralScalar type.\n");

    typedef NeuralScalar<Scalar, Utils> NScalar;

    NScalar::clear_all_blueprints();
    typedef typename NScalar::NeuralNetworkType NeuralNetwork;
    std::size_t pi = param_index_offset;
    for (std::size_t i = 0; i < specs.size(); ++i) {
      NeuralNetwork net(specs[i]);
      int net_size = net.num_parameters();
      std::vector<Scalar> net_params(params.begin() + pi,
                                     params.begin() + pi + net_size);
      net.set_parameters(net_params);
      NScalar::add_blueprint(output_inputs[i].first, output_inputs[i].second,
                             net);
      pi += net_size;
    }
  }

  template <std::size_t ParameterDim>
  void assign_estimation_parameters(
      std::array<EstimationParameter, ParameterDim> &params,
      std::size_t param_index_offset = 0,
      TinyNeuralNetworkInitialization init_method = NN_INIT_XAVIER) const {
    if (num_total_parameters() + param_index_offset > ParameterDim) {
      std::cerr << "Error: at least " << num_total_parameters() + param_index_offset 
                << " parameters are necessary for the neural augmentation.\n";
      assert(0);
      exit(1);
      return;
    }
    std::size_t pi = param_index_offset;
    for (std::size_t i = 0; i < specs.size(); ++i) {
      std::string output_name = "net";
      for (const auto &output : output_inputs[i].first) {
        output_name += "_" + output;
      }
      std::string net_prefix = output_name + "_";
      // create sensible initialization for network weights
      std::vector<double> init_weights, init_biases;
      specs[i].template initialize<double, DoubleUtils>(
          init_weights, init_biases, init_method);

      specs[i].template save_graphviz<double, DoubleUtils>(
          "init_net_" + std::to_string(i) + ".dot", output_inputs[i].second,
          output_inputs[i].first, init_weights, init_biases);

      int num_first_layer_weights =
          specs[i].num_units(1) * specs[i].input_dim();
      for (int wi = 0; wi < specs[i].num_weights(); ++wi, ++pi) {
        params[pi].name = net_prefix + "w_" + std::to_string(wi);
        params[pi].minimum = -weight_limit;
        params[pi].maximum = weight_limit;
        params[pi].value = init_weights[wi];
        if (wi < num_first_layer_weights) {
          // L1 lasso on input weights encourages input sparsity
          params[pi].l1_regularization = input_lasso_regularization;
        }
        // L2 regularization of weights in upper layers
        params[pi].l2_regularization = upper_l2_regularization;
      }
      for (int bi = 0; bi < specs[i].num_biases(); ++bi, ++pi) {
        params[pi].name = net_prefix + "b_" + std::to_string(bi);
        params[pi].minimum = -bias_limit;
        params[pi].maximum = bias_limit;
        params[pi].value = init_biases[bi];
      }
    }
  }

  std::size_t num_total_parameters() const {
    std::size_t num = 0;
    for (const auto &spec : specs) {
      num += spec.num_parameters();
    }
    return num;
  }

  void save_graphviz(std::vector<double> &params,
                     std::size_t param_index_offset = 0) const {
    std::size_t pi = param_index_offset;
    for (std::size_t i = 0; i < specs.size(); ++i) {
      std::vector<double> weights(specs[i].num_weights()),
          biases(specs[i].num_biases());
      for (int j = 0; j < specs[i].num_weights(); ++j, ++pi) {
        weights[j] = params[pi];
      }
      for (int j = 0; j < specs[i].num_biases(); ++j, ++pi) {
        biases[j] = params[pi];
      }

      specs[i].template save_graphviz<double, DoubleUtils>(
          "net_" + std::to_string(i) + ".dot", output_inputs[i].second,
          {output_inputs[i].first}, weights, biases);
    }
  }
};

#endif  // NEURAL_AUGMENTATION_H
