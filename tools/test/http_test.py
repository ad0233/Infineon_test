import http.client
import json
# Define the host and endpoint
host = "www.example.com"
endpoint = "/api/resource"
# Create the connection
conn = http.client.HTTPSConnection(host)
# Define the data to send
data = {"key": "value"}
json_data = json.dumps(data)
# Set headers
headers = {
   "Content-Type": "application/json",
   "Content-Length": str(len(json_data))
}
# Send the POST request
conn.request("POST", endpoint, body=json_data, headers=headers)
# Get the response
response = conn.getresponse()
print(f"Status: {response.status}, Reason: {response.reason}")
print(response.read().decode())
# Close the connection
conn.close()