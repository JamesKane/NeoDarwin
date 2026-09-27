# SPDX-License-Identifier: BSD-2-Clause
# Parse roadmap/backlog.yaml with a real YAML parser and check its shape:
# every epic has exactly the schema keys, ids are unique, dependencies exist,
# and the dependency graph is acyclic. Uses the system Ruby's YAML (Psych)
# until a Swift YAML reader exists in tools/.
require "yaml"
path = ARGV.fetch(0)
keys = %w[depends doc exit id owner phase status title].sort
statuses = %w[todo doing host done dropped]
epics = YAML.load_file(path).fetch("epics")
ids = epics.map { |e| e["id"] }
errors = []
epics.each do |e|
  errors << "#{e['id']}: keys #{e.keys.sort.inspect}" unless e.keys.sort == keys
  errors << "#{e['id']}: status #{e['status']}" unless statuses.include?(e["status"])
  (e["depends"] || []).each { |d| errors << "#{e['id']}: unknown dependency #{d}" unless ids.include?(d) }
end
ids.group_by(&:itself).each { |id, v| errors << "duplicate id #{id}" if v.size > 1 }
state = {}
by_id = epics.to_h { |e| [e["id"], e] }
visit = lambda do |n, stack|
  return if state[n] == :done
  raise "cycle: #{(stack + [n]).join(' -> ')}" if state[n] == :active
  state[n] = :active
  (by_id.dig(n, "depends") || []).each { |m| visit.(m, stack + [n]) }
  state[n] = :done
end
begin
  ids.each { |i| visit.(i, []) }
rescue RuntimeError => ex
  errors << ex.message
end
if errors.empty?
  counts = epics.group_by { |e| e["status"] }.map { |k, v| "#{k} #{v.size}" }.join(", ")
  puts "backlog ok: #{epics.size} epics (#{counts})"
else
  puts errors
  exit 1
end
