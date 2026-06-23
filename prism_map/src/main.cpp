#include <rclcpp/rclcpp.hpp>
#include "prism_map/mapping_node.hpp"
int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<prism_map::MappingNode>());
  rclcpp::shutdown();
  return 0;
}
