// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "OnnxPrediction.h"
#include <vector>
#include <memory>

// Uses the onnxruntime to load the modelmodelData
// into a session.
//
OnnxPrediction::OnnxPrediction(std::wstring& onnx_model_file)
    : raw_model{nullptr},
      ptr_session{nullptr},
      session{env, onnx_model_file.c_str(), empty_session_option},
      input_names{session.GetInputCount()},
      output_names{session.GetOutputCount()} {
  init();
}

// Uses the onnx to seri
//
OnnxPrediction::OnnxPrediction(onnx::ModelProto& onnx_model)
    : session{nullptr} {
  raw_model = std::shared_ptr<void>{alloc.Alloc(onnx_model.ByteSizeLong()),
                                   [this](void* ptr) {
                                     this->GetAllocator().Free(ptr);
                                   }};

  onnx_model.SerializeToArray(raw_model.get(), static_cast<int>(onnx_model.ByteSizeLong()));

  ptr_session = std::make_unique<Ort::Session>(env,
                                              raw_model.get(),
                                              onnx_model.ByteSizeLong(),
                                              empty_session_option),

  input_names.resize(ptr_session->GetInputCount());
  output_names.resize(ptr_session->GetOutputCount());

  init();
}

OnnxPrediction::OnnxPrediction(const std::vector<char>& model_data)
    : session{nullptr} {
  size_t num_bytes = model_data.size();
  raw_model = std::shared_ptr<void>{alloc.Alloc(num_bytes),
                                   [this](void* ptr) {
                                     this->GetAllocator().Free(ptr);
                                   }};
  memcpy(raw_model.get(), model_data.data(), num_bytes);
  Ort::SessionOptions so;
  so.AddConfigEntry(kOrtSessionOptionsConfigLoadModelFormat, "ORT");
  ptr_session = std::make_unique<Ort::Session>(env,
                                              raw_model.get(),
                                              num_bytes,
                                              so),

  input_names.resize(ptr_session->GetInputCount());
  output_names.resize(ptr_session->GetOutputCount());

  init();
}

// Destructor
//
OnnxPrediction::~OnnxPrediction() {
  if (ptr_session.get() == &session) {
    // Ensure the session is not deleted
    // by the unique_ptr. Because it will be deleting the stack
    //
    ptr_session.release();
  }
}

// OnnxPrediction console output format
// prints the output data.
//
std::wostream& operator<<(std::wostream& out, OnnxPrediction& pred) {
  auto pretty_print = [&out](auto ptr, Ort::Value& val) {
    out << L"[";
    std::wstring msg = L"";
    for (int i = 0; i < val.GetTensorTypeAndShapeInfo().GetElementCount(); i++) {
      out << msg << ptr[i];
      msg = L", ";
    }
    out << L"]\n";
  };

  size_t index{0};
  for (auto& val : pred.output_values) {
    out << pred.output_names[index++] << L" = ";
    pred.ProcessOutputData(pretty_print, val);
  }

  return out;
}

// Used to Generate data for predict
//
void GenerateDataForInputTypeTensor(OnnxPrediction& predict,
                                    size_t input_index, const std::string& input_name,
                                    ONNXTensorElementDataType elem_type, size_t elem_count, size_t seed) {
  (void)input_name;
  (void)input_index;

  auto pretty_print = [&input_name](auto raw_data) {
    Logger::testLog << input_name << L" = ";
    Logger::testLog << L"[";
    std::wstring msg = L"";
    for (int i = 0; i < raw_data.size(); i++) {
      Logger::testLog << msg << raw_data[i];
      msg = L", ";
    }
    Logger::testLog << L"]\n";
  };

  if (elem_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
    auto raw_data = GenerateRandomData(0.0f, elem_count, seed);
    pretty_print(raw_data);
    predict << std::move(raw_data);
  } else if (elem_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
    int32_t initial = 0;
    auto raw_data = GenerateRandomData(initial, elem_count, seed);
    pretty_print(raw_data);
    predict << std::move(raw_data);
  } else {
    throw std::exception("only floats are implemented");
  }
}

// Run the prediction
//
void OnnxPrediction::RunInference() {
  Logger::testLog << L"inference starting " << Logger::endl;
  Logger::testLog.flush();

  try {
    ptr_session->Run(run_options,
                    input_names.data(), input_values.data(),
                    input_values.size(), output_names.data(), output_values.data(),
                    output_names.size());
  } catch (...) {
    Logger::testLog << L"Something went wrong in inference " << Logger::endl;
    Logger::testLog.flush();
    throw;
  }

  Logger::testLog << L"inference completed " << Logger::endl;
  Logger::testLog.flush();
}

// Print the output values of the prediction.
//
void OnnxPrediction::PrintOutputValues() {
  Logger::testLog << L"output data:\n";
  Logger::testLog << *this;
  Logger::testLog << Logger::endl;
}

// Common initilization amongst constructors
//
void OnnxPrediction::init() {
  // Enable telemetry events
  //
  env.EnableTelemetryEvents();

  if (!ptr_session) {
    // To use one consistent value
    // across the class
    //
    ptr_session.reset(&session);
  }

  // Initialize model input names
  //
  for (int i = 0; i < ptr_session->GetInputCount(); i++) {
    input_names[i] = ptr_session->GetInputName(i, alloc);
    input_values.emplace_back(nullptr);
  }

  // Initialize model output names
  //
  for (int i = 0; i < ptr_session->GetOutputCount(); i++) {
    output_names[i] = ptr_session->GetOutputName(i, alloc);
    output_values.emplace_back(nullptr);
  }
}

// Get the allocator used by the runtime
//
Ort::AllocatorWithDefaultOptions& OnnxPrediction::GetAllocator() {
  return alloc;
}

void OnnxPrediction::SetupInput(
    InputGeneratorFunctionType GenerateData,
    size_t seed) {
  Logger::testLog << L"input data:\n";
  for (int i = 0; i < ptr_session->GetInputCount(); i++) {
    auto inputType = ptr_session->GetInputTypeInfo(i);

    if (inputType.GetONNXType() == ONNX_TYPE_TENSOR) {
      auto elem_type = inputType.GetTensorTypeAndShapeInfo().GetElementType();
      auto elem_count = inputType.GetTensorTypeAndShapeInfo().GetElementCount();

      // This can be any generic function to generate inputs
      //
      GenerateData(*this, i, std::string(input_names[i]), elem_type, elem_count, seed);

      // Update the seed in a predicatable way to get other values for different inputs
      //
      seed++;
    } else {
      std::cout << "Unsupported \n";
    }
  }
  Logger::testLog << Logger::endl;
}