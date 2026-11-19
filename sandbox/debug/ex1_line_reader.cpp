#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class LineReader {
public:
  void feed(std::string_view bytes) {
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
  }

  // Наступний повний рядок без '\n' або nullopt, якщо кінець рядка ще не
  // прийшов
  std::optional<std::string> nextLine() {
    for (std::size_t i = m_start; i < m_buffer.size(); ++i) {
      if (m_buffer[i] == '\n') {
        std::string line{m_buffer.data() + m_start, i - m_start};
        m_start = i + 1;
        return line;
      }
    }
    return std::nullopt;
  }

private:
  std::vector<char> m_buffer;
  std::size_t m_start = 0;
};

int hexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// "$<дані>*HH": XOR усіх символів між '$' і '*' має дорівнювати HH
bool checksumOk(std::string_view line) {
  if (line.size() < 4 || line[0] != '$' || line[line.size() - 3] != '*') {
    return false;
  }
  int sum = 0;
  for (std::size_t i = 1; i < line.size() - 3; ++i) {
    sum ^= static_cast<unsigned char>(line[i]);
  }
  const int high = hexDigit(line[line.size() - 2]);
  const int low = hexDigit(line[line.size() - 1]);
  return high >= 0 && low >= 0 && sum == high * 16 + low;
}

int main() {
  const std::string_view chunks[] = {
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\n$GPR",
      "MC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\n",
      "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39\n$GPVTG,054.7,T,034.4,M,"
      "005.5,N,010.2,K*48\n",
  };

  LineReader reader;
  std::vector<std::string> lines;
  for (const std::string_view chunk : chunks) {
    reader.feed(chunk);
    while (auto line = reader.nextLine()) {
      lines.push_back(std::move(*line));
    }
  }

  for (const std::string_view line : lines) {
    std::printf("%s  %.*s\n", checksumOk(line) ? "OK " : "BAD",
                static_cast<int>(line.size()), line.data());
  }
}
