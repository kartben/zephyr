/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * fbs::FbsModel, the reader of ESP-DL models (.espdl), which ESP-DL only ships
 * as a prebuilt library.
 *
 * A model is a FlatBuffers buffer following fbs_loader/espdl.fbs of ESP-DL, an
 * ONNX-like graph: nodes with their inputs, outputs and attributes, the
 * parameters (initializers), and the type and quantization exponent of every
 * value. The buffer is read in place with the small FlatBuffers reader below.
 */

#include <algorithm>
#include <cstring>
#include <string_view>

#include "fbs_model.hpp"

static const char *TAG = "FbsModel";

namespace {

/* FlatBuffers wire format: little-endian, offsets relative to where they are stored */
template <typename T> T read(const uint8_t *p)
{
	T value;

	memcpy(&value, p, sizeof(value));

	return value;
}

class Table;

class Vector {
public:
	Vector() = default;
	explicit Vector(const uint8_t *p) : m_size(read<uint32_t>(p)), m_data(p + 4)
	{
	}

	uint32_t size() const
	{
		return m_size;
	}

	const uint8_t *data() const
	{
		return m_data;
	}

	template <typename T> T scalar(uint32_t i) const
	{
		return read<T>(m_data + i * sizeof(T));
	}

	inline Table table(uint32_t i) const;

	std::string_view str(uint32_t i) const
	{
		const uint8_t *p = m_data + i * 4;

		p += read<uint32_t>(p);

		return {reinterpret_cast<const char *>(p + 4), read<uint32_t>(p)};
	}

private:
	uint32_t m_size = 0;
	const uint8_t *m_data = nullptr;
};

class Table {
public:
	Table() = default;
	explicit Table(const void *p) : m_p(static_cast<const uint8_t *>(p))
	{
	}

	explicit operator bool() const
	{
		return m_p != nullptr;
	}

	const void *ptr() const
	{
		return m_p;
	}

	template <typename T> T scalar(uint16_t field, T def) const
	{
		uint16_t off = offset(field);

		return off != 0 ? read<T>(m_p + off) : def;
	}

	/* A struct is stored inline in the table */
	const uint8_t *inline_struct(uint16_t field) const
	{
		uint16_t off = offset(field);

		return off != 0 ? m_p + off : nullptr;
	}

	Table table(uint16_t field) const
	{
		return Table(indirect(field));
	}

	Vector vec(uint16_t field) const
	{
		const uint8_t *p = indirect(field);

		return p != nullptr ? Vector(p) : Vector();
	}

	std::string_view str(uint16_t field) const
	{
		const uint8_t *p = indirect(field);

		if (p == nullptr) {
			return {};
		}

		return {reinterpret_cast<const char *>(p + 4), read<uint32_t>(p)};
	}

private:
	/* Position of a field in the table, 0 when absent. field is its vtable byte offset. */
	uint16_t offset(uint16_t field) const
	{
		if (m_p == nullptr) {
			return 0;
		}

		const uint8_t *vtable = m_p - read<int32_t>(m_p);

		return field < read<uint16_t>(vtable) ? read<uint16_t>(vtable + field) : 0;
	}

	const uint8_t *indirect(uint16_t field) const
	{
		uint16_t off = offset(field);

		if (off == 0) {
			return nullptr;
		}

		return m_p + off + read<uint32_t>(m_p + off);
	}

	const uint8_t *m_p = nullptr;
};

inline Table Vector::table(uint32_t i) const
{
	const uint8_t *p = m_data + i * 4;

	return Table(p + read<uint32_t>(p));
}

/* Vtable byte offsets of the fields of espdl.fbs: 4 + 2 * field index */
namespace model {
constexpr uint16_t model_version = 14;
constexpr uint16_t doc_string = 16;
constexpr uint16_t graph = 18;
constexpr uint16_t metadata_props = 20;
} // namespace model

namespace graph {
constexpr uint16_t node = 4;
constexpr uint16_t name = 6;
constexpr uint16_t initializer = 8;
constexpr uint16_t input = 12;
constexpr uint16_t output = 14;
constexpr uint16_t value_info = 16;
constexpr uint16_t test_inputs_value = 20;
constexpr uint16_t test_outputs_value = 22;
} // namespace graph

namespace node {
constexpr uint16_t input = 4;
constexpr uint16_t output = 6;
constexpr uint16_t name = 8;
constexpr uint16_t op_type = 10;
constexpr uint16_t attribute = 14;
} // namespace node

namespace attribute {
constexpr uint16_t name = 4;
constexpr uint16_t attr_type = 10;
constexpr uint16_t f = 12;
constexpr uint16_t i = 14;
constexpr uint16_t s = 16;
constexpr uint16_t t = 18;
constexpr uint16_t floats = 24;
constexpr uint16_t ints = 26;

/* AttributeType */
constexpr int32_t FLOAT = 1;
constexpr int32_t INT = 2;
constexpr int32_t STRING = 3;
constexpr int32_t TENSOR = 4;
constexpr int32_t FLOATS = 6;
constexpr int32_t INTS = 7;
} // namespace attribute

namespace tensor {
constexpr uint16_t dims = 4;
constexpr uint16_t data_type = 6;
constexpr uint16_t name = 16;
constexpr uint16_t raw_data = 20;
constexpr uint16_t exponents = 30;
} // namespace tensor

namespace value_info {
constexpr uint16_t name = 4;
constexpr uint16_t value_info_type = 6;
constexpr uint16_t exponents = 10;
} // namespace value_info

/* TypeInfo is a union of which only tensor_type (1) carries a tensor */
namespace type_info {
constexpr uint16_t value_type = 4;
constexpr uint16_t value = 6;
constexpr uint8_t TENSOR_TYPE = 1;
} // namespace type_info

namespace tensor_type {
constexpr uint16_t elem_type = 4;
constexpr uint16_t shape = 6;
} // namespace tensor_type

constexpr uint16_t tensor_shape_dim = 4;
constexpr uint16_t dimension_value = 4;
constexpr uint16_t dimension_value_dim_value = 6;

namespace string_entry {
constexpr uint16_t key = 4;
constexpr uint16_t value = 6;
} // namespace string_entry

std::vector<int> int64_vector(Vector v)
{
	std::vector<int> out(v.size());

	for (uint32_t i = 0; i < v.size(); i++) {
		out[i] = static_cast<int>(v.scalar<int64_t>(i));
	}

	return out;
}

std::vector<std::string> string_vector(Vector v)
{
	std::vector<std::string> out;

	out.reserve(v.size());
	for (uint32_t i = 0; i < v.size(); i++) {
		out.emplace_back(v.str(i));
	}

	return out;
}

/* An empty name stands for an omitted optional input and never names anything */
template <typename Map> Table find(const Map &map, const std::string &name)
{
	if (name.empty()) {
		return Table();
	}

	auto it = map.find(name);

	return it != map.end() ? Table(it->second) : Table();
}

Table graph_of(const void *model)
{
	return Table(model).table(model::graph);
}

/* The tensor type of a value_info, empty when it has none */
Table tensor_type_of(Table vi)
{
	Table type = vi.table(value_info::value_info_type);

	if (type.scalar<uint8_t>(type_info::value_type, 0) != type_info::TENSOR_TYPE) {
		return Table();
	}

	return type.table(type_info::value);
}

Table find_attribute(Table node, const std::string &name)
{
	Vector attrs = node.vec(node::attribute);

	for (uint32_t i = 0; i < attrs.size(); i++) {
		Table attr = attrs.table(i);

		if (attr.str(attribute::name) == name) {
			return attr;
		}
	}

	return Table();
}

/* Tensor holding the parameter data of an espdl Tensor table */
dl::TensorBase *create_tensor(Table t, bool deep, uint32_t caps)
{
	Vector raw = t.vec(tensor::raw_data);
	std::vector<int> exponents = int64_vector(t.vec(tensor::exponents));

	if (raw.data() == nullptr || raw.size() == 0) {
		ESP_LOGE(TAG, "Tensor %s has no raw data",
			 std::string(t.str(tensor::name)).c_str());
		return nullptr;
	}

	if (exponents.empty()) {
		exponents.push_back(0);
	}

	return new dl::TensorBase(int64_vector(t.vec(tensor::dims)), raw.data(), exponents,
				  static_cast<dl::dtype_t>(t.scalar<int32_t>(tensor::data_type, 0)),
				  deep, caps);
}

} // namespace

namespace fbs {

FbsModel::FbsModel(const void *data, size_t size, model_location_type_t location, bool encrypt,
		   bool rodata_move, bool auto_free, bool param_copy)
	: m_param_copy(param_copy), m_location(location), m_encrypt(encrypt),
	  m_rodata_move(rodata_move), m_auto_free(auto_free), m_size(size),
	  m_data(static_cast<const uint8_t *>(data)), m_model(nullptr)
{
	if (m_data == nullptr || m_size < 8) {
		ESP_LOGE(TAG, "Empty model");
		return;
	}

	/* Model data that PSRAM mirrors from flash is read from a copy */
	if (m_rodata_move) {
		void *copy = dl::tool::malloc_aligned(m_size, MALLOC_CAP_SPIRAM);

		if (copy == nullptr) {
			ESP_LOGE(TAG, "Cannot copy the model to PSRAM");
			m_rodata_move = false;
		} else {
			memcpy(copy, m_data, m_size);
			m_data = static_cast<const uint8_t *>(copy);
		}
	}

	m_model = m_data + read<uint32_t>(m_data);
	load_map();
}

FbsModel::~FbsModel()
{
	clear_map();
	if (m_auto_free || m_rodata_move) {
		heap_caps_free(const_cast<uint8_t *>(m_data));
	}
}

void FbsModel::load_map()
{
	Table graph = graph_of(m_model);
	Vector nodes = graph.vec(graph::node);
	Vector initializers = graph.vec(graph::initializer);

	clear_map();

	for (uint32_t i = 0; i < nodes.size(); i++) {
		Table n = nodes.table(i);

		m_name_to_node_map.emplace(n.str(node::name), n.ptr());
	}

	for (uint32_t i = 0; i < initializers.size(); i++) {
		Table t = initializers.table(i);

		m_name_to_initial_tensor_map.emplace(t.str(tensor::name), t.ptr());
	}

	for (uint16_t field : {graph::input, graph::output, graph::value_info}) {
		Vector values = graph.vec(field);

		for (uint32_t i = 0; i < values.size(); i++) {
			Table vi = values.table(i);

			m_name_to_value_info_map.emplace(vi.str(value_info::name), vi.ptr());
		}
	}

	Vector test_inputs = graph.vec(graph::test_inputs_value);
	Vector test_outputs = graph.vec(graph::test_outputs_value);

	for (uint32_t i = 0; i < test_inputs.size(); i++) {
		Table t = test_inputs.table(i);

		m_name_to_test_inputs_value_map.emplace(t.str(tensor::name), t.ptr());
	}

	for (uint32_t i = 0; i < test_outputs.size(); i++) {
		Table t = test_outputs.table(i);

		m_name_to_test_outputs_value_map.emplace(t.str(tensor::name), t.ptr());
	}
}

void FbsModel::clear_map()
{
	m_name_to_node_map.clear();
	m_name_to_initial_tensor_map.clear();
	m_name_to_value_info_map.clear();
	m_name_to_test_inputs_value_map.clear();
	m_name_to_test_outputs_value_map.clear();
}

void FbsModel::print()
{
	Table graph = graph_of(m_model);

	ESP_LOGI(TAG, "model %s, version %lld, %u nodes, %u parameters", get_model_name().c_str(),
		 (long long)get_model_version(), (unsigned)graph.vec(graph::node).size(),
		 (unsigned)graph.vec(graph::initializer).size());
}

/*
 * Nodes in an order where each runs after the nodes producing its inputs. A
 * graph already in that order, as exported models are, is returned as is.
 */
std::vector<std::string> FbsModel::topological_sort()
{
	Vector nodes = graph_of(m_model).vec(graph::node);
	uint32_t count = nodes.size();
	std::map<std::string, uint32_t> producer;
	std::vector<std::vector<uint32_t>> consumers(count);
	std::vector<uint32_t> pending(count, 0);
	std::vector<bool> done(count, false);
	std::vector<std::string> order;

	for (uint32_t i = 0; i < count; i++) {
		Vector outputs = nodes.table(i).vec(node::output);

		for (uint32_t j = 0; j < outputs.size(); j++) {
			producer.emplace(outputs.str(j), i);
		}
	}

	for (uint32_t i = 0; i < count; i++) {
		Vector inputs = nodes.table(i).vec(node::input);

		for (uint32_t j = 0; j < inputs.size(); j++) {
			auto it = producer.find(std::string(inputs.str(j)));

			if (it != producer.end() && it->second != i) {
				consumers[it->second].push_back(i);
				pending[i]++;
			}
		}
	}

	/* Always run the first ready node in graph order */
	order.reserve(count);
	while (order.size() < count) {
		uint32_t next = count;

		for (uint32_t i = 0; i < count; i++) {
			if (!done[i] && pending[i] == 0) {
				next = i;
				break;
			}
		}

		if (next == count) {
			ESP_LOGE(TAG, "The graph has a cycle");
			break;
		}

		done[next] = true;
		order.emplace_back(nodes.table(next).str(node::name));
		for (uint32_t consumer : consumers[next]) {
			pending[consumer]--;
		}
	}

	return order;
}

esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    int &ret_value)
{
	Table attr = find_attribute(find(m_name_to_node_map, node_name), attribute_name);

	if (attr.scalar<int32_t>(attribute::attr_type, 0) != attribute::INT) {
		return ESP_FAIL;
	}

	const uint8_t *i = attr.inline_struct(attribute::i);

	ret_value = i != nullptr ? static_cast<int>(read<int64_t>(i)) : 0;

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    float &ret_value)
{
	Table attr = find_attribute(find(m_name_to_node_map, node_name), attribute_name);

	if (attr.scalar<int32_t>(attribute::attr_type, 0) != attribute::FLOAT) {
		return ESP_FAIL;
	}

	const uint8_t *f = attr.inline_struct(attribute::f);

	ret_value = f != nullptr ? read<float>(f) : 0.0f;

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    std::string &ret_value)
{
	Table attr = find_attribute(find(m_name_to_node_map, node_name), attribute_name);

	if (attr.scalar<int32_t>(attribute::attr_type, 0) != attribute::STRING) {
		return ESP_FAIL;
	}

	Vector s = attr.vec(attribute::s);

	ret_value.assign(reinterpret_cast<const char *>(s.data()), s.size());

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    std::vector<int> &ret_value)
{
	Table attr = find_attribute(find(m_name_to_node_map, node_name), attribute_name);

	if (attr.scalar<int32_t>(attribute::attr_type, 0) != attribute::INTS) {
		return ESP_FAIL;
	}

	ret_value = int64_vector(attr.vec(attribute::ints));

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    std::vector<float> &ret_value)
{
	Table attr = find_attribute(find(m_name_to_node_map, node_name), attribute_name);

	if (attr.scalar<int32_t>(attribute::attr_type, 0) != attribute::FLOATS) {
		return ESP_FAIL;
	}

	Vector floats = attr.vec(attribute::floats);

	ret_value.resize(floats.size());
	for (uint32_t i = 0; i < floats.size(); i++) {
		ret_value[i] = floats.scalar<float>(i);
	}

	return ESP_OK;
}

/*
 * The enumerated attributes are strings. The value is set to its default
 * first, and an unknown string keeps it.
 */
esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    dl::quant_type_t &ret_value)
{
	std::string value;

	ret_value = dl::QUANT_TYPE_FLOAT32;
	if (get_operation_attribute(node_name, attribute_name, value) != ESP_OK) {
		return ESP_FAIL;
	}

	if (value == "S8") {
		ret_value = dl::QUANT_TYPE_SYMM_8BIT;
	} else if (value == "S16") {
		ret_value = dl::QUANT_TYPE_SYMM_16BIT;
	} else if (value == "S32") {
		ret_value = dl::QUANT_TYPE_SYMM_32BIT;
	} else if (value == "W8A16") {
		ret_value = dl::QUANT_TYPE_SYMM_W8A16;
	} else if (value != "F32") {
		ESP_LOGE(TAG, "The quant type(%s) is not support now", value.c_str());
	}

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    dl::activation_type_t &ret_value)
{
	std::string value;

	ret_value = dl::Linear;
	if (get_operation_attribute(node_name, attribute_name, value) != ESP_OK) {
		return ESP_FAIL;
	}

	if (value == "Relu") {
		ret_value = dl::ReLU;
	} else if (value == "LeakyRelu") {
		ret_value = dl::LeakyReLU;
	} else if (value == "PRelu") {
		ret_value = dl::PReLU;
	} else if (value != "Linear") {
		ESP_LOGE(TAG, "The activation type(%s) is not support now", value.c_str());
	}

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    dl::resize_mode_t &ret_value)
{
	std::string value;

	ret_value = dl::RESIZE_NEAREST;
	if (get_operation_attribute(node_name, attribute_name, value) != ESP_OK) {
		return ESP_FAIL;
	}

	if (value == "linear") {
		ret_value = dl::RESIZE_LINEAR;
	} else if (value == "cubic") {
		ret_value = dl::RESIZE_CUBIC;
	} else if (value != "nearest") {
		ESP_LOGE(TAG, "The resize mode(%s) is not support now", value.c_str());
	}

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_attribute(std::string node_name, std::string attribute_name,
					    dl::TensorBase *&ret_value)
{
	Table attr = find_attribute(find(m_name_to_node_map, node_name), attribute_name);

	if (attr.scalar<int32_t>(attribute::attr_type, 0) != attribute::TENSOR) {
		return ESP_FAIL;
	}

	ret_value = create_tensor(attr.table(attribute::t), m_param_copy, MALLOC_CAP_DEFAULT);

	return ret_value != nullptr ? ESP_OK : ESP_FAIL;
}

esp_err_t FbsModel::get_operation_input_shape(std::string node_name, int index,
					      std::vector<int> &ret_value)
{
	Vector inputs = find(m_name_to_node_map, node_name).vec(node::input);

	if (index < 0 || static_cast<uint32_t>(index) >= inputs.size()) {
		return ESP_FAIL;
	}

	std::string name(inputs.str(index));

	ret_value = is_parameter(name) ? get_tensor_shape(name) : get_value_info_shape(name);

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_output_shape(std::string node_name, int index,
					       std::vector<int> &ret_value)
{
	Vector outputs = find(m_name_to_node_map, node_name).vec(node::output);

	if (index < 0 || static_cast<uint32_t>(index) >= outputs.size()) {
		return ESP_FAIL;
	}

	ret_value = get_value_info_shape(std::string(outputs.str(index)));

	return ESP_OK;
}

esp_err_t FbsModel::get_operation_inputs_and_outputs(std::string node_name,
						     std::vector<std::string> &inputs,
						     std::vector<std::string> &outputs)
{
	Table n = find(m_name_to_node_map, node_name);

	if (!n) {
		return ESP_FAIL;
	}

	inputs = string_vector(n.vec(node::input));
	outputs = string_vector(n.vec(node::output));

	return ESP_OK;
}

std::string FbsModel::get_operation_type(std::string node_name)
{
	return std::string(find(m_name_to_node_map, node_name).str(node::op_type));
}

dl::TensorBase *FbsModel::get_operation_parameter(std::string node_name, int index, uint32_t caps)
{
	Vector inputs = find(m_name_to_node_map, node_name).vec(node::input);

	if (index < 0 || static_cast<uint32_t>(index) >= inputs.size()) {
		return nullptr;
	}

	return get_parameter(std::string(inputs.str(index)), caps);
}

dl::TensorBase *FbsModel::get_parameter(std::string tensor_name, uint32_t caps)
{
	Table t = find(m_name_to_initial_tensor_map, tensor_name);

	return t ? create_tensor(t, m_param_copy, caps) : nullptr;
}

esp_err_t FbsModel::get_operation_lut_name(std::string node_name, std::string &lut_name,
					   std::string attribute_name)
{
	std::string name;

	if (get_operation_attribute(node_name, attribute_name, name) != ESP_OK ||
	    !is_parameter(name)) {
		return ESP_FAIL;
	}

	lut_name = name;

	return ESP_OK;
}

dl::TensorBase *FbsModel::get_operation_lut(std::string node_name, uint32_t caps,
					    std::string attribute_name)
{
	std::string name;

	if (get_operation_lut_name(node_name, name, attribute_name) != ESP_OK) {
		return nullptr;
	}

	return get_parameter(name, caps);
}

bool FbsModel::is_parameter(std::string name)
{
	return static_cast<bool>(find(m_name_to_initial_tensor_map, name));
}

const void *FbsModel::get_tensor_raw_data(std::string tensor_name)
{
	return find(m_name_to_initial_tensor_map, tensor_name).vec(tensor::raw_data).data();
}

dl::dtype_t FbsModel::get_tensor_dtype(std::string tensor_name)
{
	Table t = find(m_name_to_initial_tensor_map, tensor_name);

	return static_cast<dl::dtype_t>(t.scalar<int32_t>(tensor::data_type, 0));
}

std::vector<int> FbsModel::get_tensor_shape(std::string tensor_name)
{
	return int64_vector(find(m_name_to_initial_tensor_map, tensor_name).vec(tensor::dims));
}

std::vector<int> FbsModel::get_tensor_exponents(std::string tensor_name)
{
	return int64_vector(find(m_name_to_initial_tensor_map, tensor_name).vec(tensor::exponents));
}

dl::dtype_t FbsModel::get_value_info_dtype(std::string var_name)
{
	Table type = tensor_type_of(find(m_name_to_value_info_map, var_name));

	return static_cast<dl::dtype_t>(type.scalar<int32_t>(tensor_type::elem_type, 0));
}

std::vector<int> FbsModel::get_value_info_shape(std::string var_name)
{
	Table shape =
		tensor_type_of(find(m_name_to_value_info_map, var_name)).table(tensor_type::shape);
	Vector dims = shape.vec(tensor_shape_dim);
	std::vector<int> ret(dims.size());

	for (uint32_t i = 0; i < dims.size(); i++) {
		Table value = dims.table(i).table(dimension_value);

		ret[i] = static_cast<int>(value.scalar<int64_t>(dimension_value_dim_value, 0));
	}

	return ret;
}

int FbsModel::get_value_info_exponent(std::string var_name)
{
	Vector exponents = find(m_name_to_value_info_map, var_name).vec(value_info::exponents);

	return exponents.size() > 0 ? static_cast<int>(exponents.scalar<int64_t>(0)) : 0;
}

const void *FbsModel::get_test_input_tensor_raw_data(std::string tensor_name)
{
	return find(m_name_to_test_inputs_value_map, tensor_name).vec(tensor::raw_data).data();
}

const void *FbsModel::get_test_output_tensor_raw_data(std::string tensor_name)
{
	return find(m_name_to_test_outputs_value_map, tensor_name).vec(tensor::raw_data).data();
}

dl::TensorBase *FbsModel::get_test_input_tensor(std::string tensor_name)
{
	Table t = find(m_name_to_test_inputs_value_map, tensor_name);

	if (!t) {
		ESP_LOGE(TAG, "No test input %s", tensor_name.c_str());
		return nullptr;
	}

	return create_tensor(t, true, MALLOC_CAP_DEFAULT);
}

dl::TensorBase *FbsModel::get_test_output_tensor(std::string tensor_name)
{
	Table t = find(m_name_to_test_outputs_value_map, tensor_name);

	if (!t) {
		ESP_LOGE(TAG, "No test output %s", tensor_name.c_str());
		return nullptr;
	}

	return create_tensor(t, true, MALLOC_CAP_DEFAULT);
}

std::vector<std::string> FbsModel::get_test_outputs_name()
{
	Vector outputs = graph_of(m_model).vec(graph::test_outputs_value);
	std::vector<std::string> names;

	for (uint32_t i = 0; i < outputs.size(); i++) {
		names.emplace_back(outputs.table(i).str(tensor::name));
	}

	return names;
}

std::vector<std::string> FbsModel::get_graph_inputs()
{
	Vector inputs = graph_of(m_model).vec(graph::input);
	std::vector<std::string> names;

	/* Parameters listed as graph inputs are not inputs of the model */
	for (uint32_t i = 0; i < inputs.size(); i++) {
		std::string name(inputs.table(i).str(value_info::name));

		if (!is_parameter(name)) {
			names.push_back(name);
		}
	}

	return names;
}

std::vector<std::string> FbsModel::get_graph_outputs()
{
	Vector outputs = graph_of(m_model).vec(graph::output);
	std::vector<std::string> names;

	for (uint32_t i = 0; i < outputs.size(); i++) {
		names.emplace_back(outputs.table(i).str(value_info::name));
	}

	return names;
}

std::string FbsModel::get_model_name()
{
	return std::string(graph_of(m_model).str(graph::name));
}

int64_t FbsModel::get_model_version()
{
	return Table(m_model).scalar<int64_t>(model::model_version, 0);
}

std::string FbsModel::get_model_doc_string()
{
	return std::string(Table(m_model).str(model::doc_string));
}

std::string FbsModel::get_model_metadata_prop(const std::string &key)
{
	Vector props = Table(m_model).vec(model::metadata_props);

	for (uint32_t i = 0; i < props.size(); i++) {
		Table prop = props.table(i);

		if (prop.str(string_entry::key) == key) {
			return std::string(prop.str(string_entry::value));
		}
	}

	return "";
}

void FbsModel::get_model_size(size_t *internal_size, size_t *psram_size, size_t *psram_rodata_size,
			      size_t *flash_size)
{
	*internal_size = 0;
	*psram_size = 0;
	*psram_rodata_size = 0;
	*flash_size = 0;

	if (m_rodata_move) {
		*psram_rodata_size = m_size;
	} else if (m_location == MODEL_LOCATION_IN_SDCARD || m_encrypt) {
		*psram_size = m_size;
	} else {
		*flash_size = m_size;
	}
}

} // namespace fbs
