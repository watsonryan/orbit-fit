#include <orbitfit/io.hpp>

int main() {
  const auto example = orbitfit::make_example_request();
  if (!example) return 1;
  const auto request = orbitfit::read_request(*example);
  if (!request) return 2;
  const auto fit = orbitfit::fit(request->samples, request->options);
  return fit && fit->accepted ? 0 : 3;
}
