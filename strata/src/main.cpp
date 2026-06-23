#include <rclcpp/rclcpp.hpp>
#include "strata/mapping_node.hpp"
int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<strata::MappingNode>());
  rclcpp::shutdown();
  return 0;
}
