# Conflict Fixture

This fixture demonstrates a version conflict that cannot be resolved.

## The Conflict

- `sinatra ~> 4.0` requires `rack ~> 3.0`
- The Gemfile also requires `rack ~> 2.0`
- These constraints are mutually exclusive

## Expected Behavior

Both wow and Bundler should:
1. Detect the conflict during resolution
2. Provide a human-readable error explaining WHY

## Expected Error Message

```
Because sinatra >= 4.0 depends on rack >= 3.0
  and Gemfile depends on rack ~> 2.0,
  sinatra >= 4.0 is incompatible with Gemfile.
And because Gemfile depends on sinatra ~> 4.0,
  version solving has failed.
```

This is the key advantage of PubGrub over Bundler's Molinillo:
clear, actionable error messages that explain the conflict chain.
