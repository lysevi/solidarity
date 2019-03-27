#include <msgpack.hpp>

#include <catch.hpp>

#ifdef ENABLE_BENCHMARKS

class myclass {
public:
  std::string m_str;
  std::vector<int> m_vec;

public:
  MSGPACK_DEFINE(m_str, m_vec);
};

TEST_CASE("msgpack", "[bench]") {
  BENCHMARK("id::string") {
    msgpack::type::tuple<int, std::string> src(1, "example");

    // serialize the object into the buffer.
    // any classes that implements write(const char*,size_t) can be a buffer.
    std::stringstream buffer;
    msgpack::pack(buffer, src);

    // send the buffer ...
    buffer.seekg(0);

    // deserialize the buffer into msgpack::object instance.
    std::string str(buffer.str());

    msgpack::object_handle oh = msgpack::unpack(str.data(), str.size());

    // deserialized object is valid during the msgpack::object_handle instance is alive.
    msgpack::object deserialized = oh.get();

    // convert msgpack::object instance into the original type.
    // if the type is mismatched, it throws msgpack::type_error exception.
    msgpack::type::tuple<int, std::string> dst;
    deserialized.convert(dst);

    // or create the new instance
    msgpack::type::tuple<int, std::string> dst2
        = deserialized.as<msgpack::type::tuple<int, std::string>>();
  }

  BENCHMARK("my_class") {
    std::vector<myclass> vec;
    size_t vsize = 100;

    vec.resize(vsize);
    for (size_t i = 0; i < vec.size(); ++i) {
      vec[i].m_str = "Hello world";
      vec[i].m_vec.push_back(i);
    }
    // you can serialize myclass directly
    msgpack::sbuffer sbuf;
    msgpack::pack(sbuf, vec);

    msgpack::object_handle oh = msgpack::unpack(sbuf.data(), sbuf.size());

    msgpack::object obj = oh.get();

    // you can convert object to myclass directly
    std::vector<myclass> rvec;
    obj.convert(rvec);
  }
}
#endif