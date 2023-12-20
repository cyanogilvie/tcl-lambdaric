set ::events {
	apig_v1_mvheaders {
		{
			"requestContext": {
				"elb": {
					"targetGroupArn": "arn:aws:elasticloadbalancing:region:123456789012:targetgroup/my-target-group/6d0ecf831eec9f09"
				}
			},
			"httpMethod": "GET",
			"path": "/\u306ffoo%2fbar/baz",
			"multiValueQueryStringParameters": {"foo":["b\nar"],"b%3Daz":["q%3Duux"]},
			"multiValueHeaders": {
				"accept": ["text/html,application/xhtml+xml"],
				"accept-language": ["en-US,en;q=0.8"],
				"content-type": ["text/javascript"],
				"cookie": ["cookie1=value1","cookie2=value2"],
				"host": ["lambda-846800462-us-east-2.elb.amazonaws.com"],
				"user-agent": ["Mozilla/5.0 (Macintosh; Intel Mac OS X 10_11_6)"],
				"x-amzn-trace-id": ["Root=1-5bdb40ca-556d8b0c50dc66f0511bf520"],
				"x-rl-edge-host": ["dev-int.rubylane.com"],
				"x-forwarded-for": ["72.21.198.66"],
				"x-forwarded-port": ["443"],
				"x-forwarded-proto": ["https"]
			},
			"isBase64Encoded": false,
			"body": "request_body"
		}
	}
}

